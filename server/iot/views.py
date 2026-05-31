from django.utils import timezone
from datetime import datetime, timedelta
import hashlib
import json
import logging
import math
from django.shortcuts import get_object_or_404
from django.conf import settings
from django.contrib.auth.models import User
from django.contrib.auth import update_session_auth_hash
from django.contrib.auth.tokens import default_token_generator
from django.contrib.auth.password_validation import validate_password
from django.core.exceptions import ValidationError
from django.core.cache import cache
from django.core.mail import EmailMessage, get_connection
from django.core.validators import validate_email
from django.utils.encoding import force_bytes, force_str
from django.utils.http import urlsafe_base64_decode, urlsafe_base64_encode
from django.utils.dateparse import parse_datetime
from zoneinfo import available_timezones
from rest_framework.views import APIView
from rest_framework.response import Response
from rest_framework import status
from rest_framework.permissions import IsAdminUser, IsAuthenticated

from .models import Device, DeviceConfig, Alert, Log, DeviceEvent, SystemSettings, DeviceSensorState, FeedNowCommand, default_grain_types, normalize_grain_config
from .models import UserApproval, AdminRoleVote
from .serializers import DeviceSerializer, DeviceConfigSerializer, AlertSerializer, LogSerializer, DeviceEventSerializer, SystemSettingsSerializer, DeviceSensorStateSerializer, FeedNowCommandSerializer
from .config_sync import sync_schedules_from_payload, sync_thresholds_from_payload
import secrets

from .serializers import ScheduleSerializer
from .models import Schedule
from django.forms.models import model_to_dict


logger = logging.getLogger(__name__)
DEFAULT_IMPORTANT_LOG_KEYWORDS = ['critical', 'error', 'fault', 'warning', 'alert', 'offline', 'fail']
POWER_ALERT_DEDUP_SECONDS = 45
DEVICE_CONNECTION_TIMEOUT_SECONDS = 60
DEVICE_CONNECTION_LOSS_ALERT_TYPE = 'device_connection_loss'
DEVICE_CONNECTION_RESTORED_ALERT_TYPE = 'device_connection_restored'
DEVICE_CONNECTION_LOSS_LOG_TYPE = 'device_connection_loss'
DEVICE_CONNECTION_RESTORED_LOG_TYPE = 'device_connection_restored'
LOW_BATTERY_SHUTDOWN_ALERT_TYPE = 'low_battery_shutdown'
LOW_BATTERY_SHUTDOWN_RESOLVED_ALERT_TYPE = 'low_battery_shutdown_resolved'
LOW_BATTERY_SHUTDOWN_LOG_TYPE = 'shutdown'
LOW_BATTERY_SHUTDOWN_RESOLVE_AFTER_SECONDS = 5
EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS = 60


def _derive_log_category(log_type):
    return 'feeding' if str(log_type or '').strip().lower() == 'feeding' else 'system'


def _get_system_timezone_name():
    return SystemSettings.get_timezone_name()


def _serialize_device_schedules(device):
    schedules = []
    for s in Schedule.objects.filter(device=device).order_by('id'):
        schedules.append({
            'id': s.id,
            'schedule_name': s.schedule_name,
            'enabled': s.enabled,
            'days': s.days,
            'time': s.time,
            'feeding_amount_kg': s.feeding_amount_kg,
            'last_updated': s.last_updated.isoformat(),
        })
    return schedules


def _normalize_dt(dt):
    if dt is None:
        return None
    if timezone.is_naive(dt):
        return timezone.make_aware(dt)
    return dt


def _parse_optional_dt(value):
    if not value:
        return None
    parsed = parse_datetime(value)
    return _normalize_dt(parsed) if parsed else None


def _get_effective_max_capacity():
    settings_obj = SystemSettings.get_effective()
    return {
        'max_feeds_capacity_kg': settings_obj.max_feeds_capacity_kg,
        'max_feeds_capacity_updated_at': _normalize_dt(settings_obj.max_feeds_capacity_updated_at).isoformat(),
        'max_feeds_capacity_updated_by': settings_obj.max_feeds_capacity_updated_by,
    }


def _compute_max_single_feed_kg(device, cfg=None):
    """Return the best-known max single-feed kg for a device.

    Preference order:
    - explicit value in DeviceConfig.config['max_single_feed_kg'] (if numeric)
    - derive from SystemSettings.max_feeds_capacity_kg * feeder_level_pct/100 (using current sensor state)
    """
    try:
        # If caller provided an up-to-date cfg, prefer its explicit field
        if cfg is None:
            cfg = DeviceConfig.objects.filter(device=device).first()
        if cfg is not None and isinstance(cfg.config, dict):
            v = cfg.config.get('max_single_feed_kg')
            try:
                return float(v)
            except (TypeError, ValueError):
                pass

        # Fallback: derive from system max capacity and current feeder sensor level
        settings_obj = SystemSettings.get_solo()
        max_capacity = float(settings_obj.max_feeds_capacity_kg)
        sensor = DeviceSensorState.objects.filter(device=device).first()
        feeder_pct = 100.0
        if sensor is not None and sensor.feeder_level_pct is not None:
            try:
                feeder_pct = float(sensor.feeder_level_pct)
            except (TypeError, ValueError):
                feeder_pct = 100.0

        derived = max_capacity * max(0.0, min(100.0, feeder_pct)) / 100.0
        return float(derived)
    except Exception:
        # On unexpected errors, fall back to system capacity
        try:
            return float(SystemSettings.get_solo().max_feeds_capacity_kg)
        except Exception:
            return 1.0


def _get_effective_grain_types():
    settings_obj = SystemSettings.get_effective()
    return {
        'grain_types': settings_obj.get_grain_types() if hasattr(settings_obj, 'get_grain_types') else default_grain_types(),
    }


def _resolve_grain_feed_ms_per_kg_from_config(config):
    cfg = normalize_grain_config(config)
    return cfg.get('feed_ms_per_kg')


def _apply_effective_grain_config(config, grain_types=None, grain_type=None, grain_type_index=None):
    if not isinstance(config, dict):
        return

    effective_grain_types = grain_types
    if effective_grain_types is None:
        effective_grain_types = _get_effective_grain_types()['grain_types']

    normalized = normalize_grain_config(
        config,
        grain_types=effective_grain_types,
        grain_type=grain_type if grain_type is not None else config.get('grain_type'),
        grain_type_index=grain_type_index if grain_type_index is not None else config.get('grain_type_index'),
    )
    config.clear()
    config.update(normalized)


def _grain_type_index_conflicts(grain_types, grain_type, grain_type_index):
    if grain_type is None or grain_type_index is None:
        return False

    try:
        idx = int(grain_type_index)
    except (TypeError, ValueError):
        return False

    normalized_types = [item for item in (grain_types or []) if isinstance(item, dict)]
    if idx < 0 or idx >= len(normalized_types):
        return False

    indexed_name = str(normalized_types[idx].get('grain_type') or '').strip()
    requested_name = str(grain_type or '').strip()
    return bool(indexed_name and requested_name and indexed_name != requested_name)


def _maybe_update_system_grain_type(value, grain_type_index=None):
    settings_obj = SystemSettings.get_solo()
    grain_types = settings_obj.get_grain_types() if hasattr(settings_obj, 'get_grain_types') else default_grain_types()
    selected = normalize_grain_config(
        {'grain_type': value, 'grain_type_index': grain_type_index},
        grain_types=grain_types,
        grain_type=value,
        grain_type_index=grain_type_index,
    )
    grain_type = str(selected.get('grain_type') or '').strip()
    if not grain_type:
        return False

    available_types = {str(item.get('grain_type') or '') for item in grain_types if isinstance(item, dict)}
    if grain_type not in available_types:
        return False
    if settings_obj.grain_type == grain_type:
        return False

    settings_obj._changed_system_setting_fields = {'grain_type'}
    settings_obj.grain_type = grain_type
    settings_obj.save(update_fields=['grain_type', 'updated_at'])
    return True


def _get_effective_grain_profile():
    settings_obj = SystemSettings.get_effective()
    grain_types = settings_obj.get_grain_types() if hasattr(settings_obj, 'get_grain_types') else default_grain_types()
    normalized = normalize_grain_config(
        {'grain_type': settings_obj.grain_type},
        grain_types=grain_types,
        grain_type=settings_obj.grain_type,
    )
    return {
        'grain_type': normalized.get('grain_type'),
        'grain_type_index': normalized.get('grain_type_index'),
        'feed_ms_per_kg': normalized.get('feed_ms_per_kg'),
    }


def _get_effective_thresholds():
    settings_obj = SystemSettings.get_effective()
    return {
        'feeder_low_threshold_pct': settings_obj.feeder_low_threshold_pct,
        'feeder_high_threshold_pct': settings_obj.feeder_high_threshold_pct,
        'water_low_threshold_pct': settings_obj.water_low_threshold_pct,
        'water_high_threshold_pct': settings_obj.water_high_threshold_pct,
    }


def _get_effective_alert_thresholds():
    settings_obj = SystemSettings.get_effective()
    return {
        'alert_feeder_low_threshold_pct': settings_obj.alert_feeder_low_threshold_pct,
        'alert_feeder_high_threshold_pct': settings_obj.alert_feeder_high_threshold_pct,
        'alert_water_low_threshold_pct': settings_obj.alert_water_low_threshold_pct,
        'alert_water_high_threshold_pct': settings_obj.alert_water_high_threshold_pct,
    }


def _get_effective_battery_shutdown_threshold():
    settings_obj = SystemSettings.get_effective()
    return {
        'low_battery_shutdown_v': settings_obj.low_battery_shutdown_v,
    }


def _maybe_update_max_capacity(value, source, candidate_dt=None):
    if value is None:
        return _get_effective_max_capacity(), False

    try:
        numeric_value = float(value)
    except (TypeError, ValueError):
        return _get_effective_max_capacity(), False

    if numeric_value <= 0:
        return _get_effective_max_capacity(), False

    settings_obj = SystemSettings.get_solo()
    server_dt = _normalize_dt(settings_obj.max_feeds_capacity_updated_at) or timezone.make_aware(datetime(1970, 1, 1, 0, 0, 0))
    incoming_dt = _normalize_dt(candidate_dt) or timezone.now()

    if incoming_dt >= server_dt:
        settings_obj.max_feeds_capacity_kg = numeric_value
        settings_obj.max_feeds_capacity_updated_at = incoming_dt
        settings_obj.max_feeds_capacity_updated_by = (source or 'unknown')[:16]
        settings_obj.save(update_fields=['max_feeds_capacity_kg', 'max_feeds_capacity_updated_at', 'max_feeds_capacity_updated_by', 'updated_at'])
        return _get_effective_max_capacity(), True

    return _get_effective_max_capacity(), False


def _sync_deviceconfig_schedules(device):
    """Ensure DeviceConfig.config['schedules'] mirrors Schedule rows for the device."""
    cfg, _ = DeviceConfig.objects.get_or_create(device=device)
    cfg.config = cfg.config or {}
    cfg.config['schedules'] = _serialize_device_schedules(device)
    cfg.config['system_timezone'] = _get_system_timezone_name()
    cfg.config.update(_get_effective_max_capacity())
    cfg.config.update(_get_effective_thresholds())
    cfg.config.update(_get_effective_alert_thresholds())
    cfg.config.update(_get_effective_battery_shutdown_threshold())
    # Ensure the per-device max single-feed ceiling is present (prefer device-provided value)
    try:
        cfg.config['max_single_feed_kg'] = _compute_max_single_feed_kg(device, cfg=cfg)
    except Exception:
        pass
    cfg.updated_by = 'server'
    cfg.last_updated = timezone.now()
    cfg.save()


def _merge_device_config(existing_config, incoming_config):
    merged = dict(existing_config or {}) if isinstance(existing_config, dict) else {}
    if isinstance(incoming_config, dict):
        merged.update(incoming_config)
    return merged


def _get_device_sensor_state(device):
    """Get current sensor readings for a device (or defaults if not set)."""
    try:
        sensor = DeviceSensorState.objects.get(device=device)
        return {
            'feeder_level_pct': sensor.feeder_level_pct,
            'water_level_pct': sensor.water_level_pct,
            'battery_voltage_v': sensor.battery_voltage_v,
            'feed_sufficient': sensor.feed_sufficient,
            'feed_current_kg': sensor.feed_current_kg,
            'feed_required_next_kg': sensor.feed_required_next_kg,
            'last_reported_at': sensor.last_reported_at.isoformat() if sensor.last_reported_at else None,
        }
    except DeviceSensorState.DoesNotExist:
        return {
            'feeder_level_pct': 100.0,
            'water_level_pct': 100.0,
            'battery_voltage_v': None,
            'feed_sufficient': None,
            'feed_current_kg': None,
            'feed_required_next_kg': None,
            'last_reported_at': None,
        }


def _normalize_event_key_part(value):
    text = '' if value is None else str(value).strip()
    if not text:
        return ''
    if len(text) <= 128:
        return text
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def _coerce_event_sequence(value):
    if value in (None, ''):
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _coerce_optional_bool(value):
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    text = str(value).strip().lower()
    if text in ('true', '1', 'yes', 'y', 'on'):
        return True
    if text in ('false', '0', 'no', 'n', 'off'):
        return False
    return None


def _build_device_event_id(device, event_type, occurred_at, payload, source='esp32', boot_id='', sequence=None, explicit_event_id=None):
    event_id = _normalize_event_key_part(explicit_event_id)
    if event_id:
        return event_id

    if boot_id and sequence is not None:
        return _normalize_event_key_part(f'{device.device_id}|{boot_id}|{sequence}|{event_type}') or hashlib.sha256(
            f'{device.device_id}|{boot_id}|{sequence}|{event_type}'.encode('utf-8')
        ).hexdigest()

    occurred_stamp = _normalize_dt(occurred_at).isoformat() if occurred_at else ''
    payload_json = json.dumps(payload or {}, sort_keys=True, separators=(',', ':'), ensure_ascii=False)
    raw_key = f'{device.device_id}|{event_type}|{occurred_stamp}|{boot_id}|{sequence if sequence is not None else ""}|{source}|{payload_json}'
    return hashlib.sha256(raw_key.encode('utf-8')).hexdigest()


def _record_device_event(device, event_type, payload, occurred_at, *, event_id=None, boot_id='', sequence=None, source='esp32', delivery_status='accepted'):
    payload = payload if isinstance(payload, dict) else {}
    resolved_event_id = _build_device_event_id(
        device=device,
        event_type=event_type,
        occurred_at=occurred_at,
        payload=payload,
        source=source,
        boot_id=boot_id,
        sequence=sequence,
        explicit_event_id=event_id,
    )

    event, created = DeviceEvent.objects.get_or_create(
        device=device,
        event_id=resolved_event_id,
        defaults={
            'event_type': event_type,
            'occurred_at': occurred_at,
            'boot_id': boot_id or '',
            'sequence': sequence,
            'source': source or 'esp32',
            'payload': payload,
            'delivery_status': delivery_status,
        },
    )

    return event, created


def _build_connection_event_payload(device, event_type, timestamp, *, trigger=None, source='server', previous_last_seen=None, offline_seconds=None, extra=None):
    payload = {
        'event': event_type,
        'device_id': device.device_id,
        'source': source,
    }
    if trigger:
        payload['trigger'] = trigger
    if previous_last_seen is not None:
        payload['previous_last_seen'] = _normalize_dt(previous_last_seen).isoformat()
    if offline_seconds is not None:
        payload['offline_seconds'] = int(max(0, offline_seconds))
    if timestamp is not None:
        payload['timestamp'] = _normalize_dt(timestamp).isoformat()
    if isinstance(extra, dict):
        payload.update(extra)
    return payload


def _create_connection_log_and_alert(device, alert_type, log_type, timestamp, payload, *, resolved=False):
    alert = Alert.objects.create(device=device, alert_type=alert_type, timestamp=timestamp, resolved=resolved)
    log_payload = dict(payload or {})
    log_payload['alert_id'] = alert.id
    try:
        _ingest_log_projection(
            device=device,
            log_type=log_type,
            payload=log_payload,
            log_timestamp=timestamp,
            source='server',
        )
    except Exception:
        logger.exception('Failed to create %s log', log_type)
    email_result = _send_alert_notification(alert, is_refresh=False)
    return alert, email_result


def _create_or_refresh_shutdown_alert(device, timestamp, payload, *, alert_type=LOW_BATTERY_SHUTDOWN_ALERT_TYPE, log_type=None, create_log=False):
    shutdown_payload = dict(payload or {})
    shutdown_payload.setdefault('event', alert_type)

    existing_alert = Alert.objects.filter(
        device=device,
        alert_type=alert_type,
        resolved=False,
    ).order_by('-last_updated', '-id').first()

    if existing_alert:
        age_seconds = (timestamp - existing_alert.last_updated).total_seconds()
        if age_seconds <= POWER_ALERT_DEDUP_SECONDS:
            if create_log and log_type:
                try:
                    existing_log = Log.objects.filter(device=device, log_type=log_type).order_by('-id').first()
                    if existing_log:
                        existing_log.timestamp = timestamp
                        existing_log.refresh_count += 1
                        existing_log.payload = existing_log.payload or {}
                        existing_log.payload.update(shutdown_payload)
                        existing_log.save(update_fields=['timestamp', 'payload', 'refresh_count', 'last_updated'])
                    else:
                        Log.objects.create(
                            device=device,
                            log_type=log_type,
                            log_category=_derive_log_category(log_type),
                            payload=shutdown_payload,
                            timestamp=timestamp,
                        )
                except Exception:
                    logger.exception('Failed to create/update %s log', log_type)
            return existing_alert, True, True, None

        existing_alert.timestamp = timestamp
        existing_alert.refresh_count += 1
        existing_alert.payload = shutdown_payload
        existing_alert.save(update_fields=['timestamp', 'refresh_count', 'payload', 'last_updated'])
        email_result = _send_alert_notification(existing_alert, is_refresh=True)
        if create_log and log_type:
            try:
                existing_log = Log.objects.filter(device=device, log_type=log_type).order_by('-id').first()
                if existing_log:
                    existing_log.timestamp = timestamp
                    existing_log.refresh_count += 1
                    existing_log.payload = existing_log.payload or {}
                    existing_log.payload.update(shutdown_payload)
                    existing_log.save(update_fields=['timestamp', 'payload', 'refresh_count', 'last_updated'])
                else:
                    Log.objects.create(
                        device=device,
                        log_type=log_type,
                        log_category=_derive_log_category(log_type),
                        payload=shutdown_payload,
                        timestamp=timestamp,
                    )
            except Exception:
                logger.exception('Failed to create/update %s log', log_type)
        return existing_alert, True, False, email_result

    alert = Alert.objects.create(device=device, alert_type=alert_type, timestamp=timestamp, payload=shutdown_payload)
    if create_log and log_type:
        try:
            Log.objects.create(
                device=device,
                log_type=log_type,
                log_category=_derive_log_category(log_type),
                payload=shutdown_payload,
                timestamp=timestamp,
            )
        except Exception:
            logger.exception('Failed to create %s log', log_type)
    email_result = _send_alert_notification(alert, is_refresh=False)
    return alert, False, False, email_result


def _resolve_stale_shutdown_alerts(device=None, now_ts=None, *, grace_seconds=LOW_BATTERY_SHUTDOWN_RESOLVE_AFTER_SECONDS):
    now_ts = _normalize_dt(now_ts) or timezone.now()
    cutoff = now_ts - timedelta(seconds=max(1, int(grace_seconds)))

    query = Alert.objects.filter(
        alert_type=LOW_BATTERY_SHUTDOWN_ALERT_TYPE,
        resolved=False,
        timestamp__lte=cutoff,
    ).order_by('timestamp', 'id')
    if device is not None:
        query = query.filter(device=device)

    resolved_count = 0
    for alert in query:
        resolved_payload = dict(alert.payload or {})
        resolved_payload.setdefault('event', LOW_BATTERY_SHUTDOWN_RESOLVED_ALERT_TYPE)
        resolved_payload['resolved_at'] = now_ts.isoformat()
        resolved_payload['resolved_reason'] = 'timeout'
        resolved_payload['resolved_after_seconds'] = max(0, int((now_ts - alert.timestamp).total_seconds()))

        alert.resolved = True
        alert.payload = resolved_payload
        alert.save(update_fields=['resolved', 'payload', 'last_updated'])

        try:
            Alert.objects.create(
                device=alert.device,
                alert_type=LOW_BATTERY_SHUTDOWN_RESOLVED_ALERT_TYPE,
                timestamp=now_ts,
                resolved=True,
                payload=resolved_payload,
            )
        except Exception:
            logger.exception('Failed to create low battery shutdown resolved alert')

        try:
            _ingest_log_projection(
                device=alert.device,
                log_type=f'{LOW_BATTERY_SHUTDOWN_LOG_TYPE}_resolved',
                payload=resolved_payload,
                log_timestamp=now_ts,
                source='server',
            )
        except Exception:
            logger.exception('Failed to create low battery shutdown resolved log')

        resolved_count += 1

    return resolved_count


def _mark_device_connection_lost(device, detected_at=None, *, trigger='heartbeat_timeout'):
    detected_at = _normalize_dt(detected_at) or timezone.now()
    current_status = str(device.connection_status or 'unknown').strip().lower()
    if current_status == 'disconnected':
        return None, False, None

    previous_last_seen = device.last_seen
    offline_seconds = None
    if previous_last_seen is not None:
        offline_seconds = max(0, int((detected_at - _normalize_dt(previous_last_seen)).total_seconds()))

    device.connection_status = 'disconnected'
    device.save(update_fields=['connection_status'])

    payload = _build_connection_event_payload(
        device,
        'connection_lost',
        detected_at,
        trigger=trigger,
        source='server',
        previous_last_seen=previous_last_seen,
        offline_seconds=offline_seconds,
    )
    alert, email_result = _create_connection_log_and_alert(
        device,
        DEVICE_CONNECTION_LOSS_ALERT_TYPE,
        DEVICE_CONNECTION_LOSS_LOG_TYPE,
        detected_at,
        payload,
        resolved=False,
    )
    return alert, True, email_result


def _mark_device_connection_seen(device, seen_at=None, *, trigger='heartbeat', source='esp32', extra_payload=None):
    seen_at = _normalize_dt(seen_at) or timezone.now()
    previous_status = str(device.connection_status or 'unknown').strip().lower()
    previous_last_seen = device.last_seen
    device.last_seen = seen_at
    device.connection_status = 'connected'
    device.save(update_fields=['last_seen', 'connection_status'])

    if previous_status != 'disconnected':
        return None, False, None

    offline_seconds = None
    if previous_last_seen is not None:
        offline_seconds = max(0, int((seen_at - _normalize_dt(previous_last_seen)).total_seconds()))

    payload = _build_connection_event_payload(
        device,
        'connection_restored',
        seen_at,
        trigger=trigger,
        source=source,
        previous_last_seen=previous_last_seen,
        offline_seconds=offline_seconds,
        extra=extra_payload,
    )

    Alert.objects.filter(
        device=device,
        alert_type=DEVICE_CONNECTION_LOSS_ALERT_TYPE,
        resolved=False,
    ).update(resolved=True, last_updated=seen_at)

    alert, email_result = _create_connection_log_and_alert(
        device,
        DEVICE_CONNECTION_RESTORED_ALERT_TYPE,
        DEVICE_CONNECTION_RESTORED_LOG_TYPE,
        seen_at,
        payload,
        resolved=True,
    )
    return alert, True, email_result


def _ingest_log_projection(device, log_type, payload, log_timestamp, *, event_id=None, boot_id='', sequence=None, source='esp32', delivery_status='accepted'):
    event, created = _record_device_event(
        device=device,
        event_type=log_type,
        payload=payload,
        occurred_at=log_timestamp,
        event_id=event_id,
        boot_id=boot_id,
        sequence=sequence,
        source=source,
        delivery_status=delivery_status,
    )

    if not created:
        existing_log = Log.objects.filter(device=device, log_type=log_type).order_by('-id').first()
        return event, existing_log, True, False

    should_stack = log_type in {'feeding', 'feed_now', DEVICE_CONNECTION_LOSS_LOG_TYPE, DEVICE_CONNECTION_RESTORED_LOG_TYPE}
    existing_log = Log.objects.filter(device=device, log_type=log_type).order_by('-id').first()

    # Attempt to correlate feeding <-> feed_now when firmware supplies command_id/trigger
    try:
        cmd_id = None
        trigger = None
        if isinstance(payload, dict):
            cmd_id = payload.get('command_id') or (payload.get('payload') or {}).get('command_id')
            trigger = payload.get('trigger') or (payload.get('payload') or {}).get('event')
        if cmd_id is not None:
            try:
                cmd_id_int = int(cmd_id)
            except (TypeError, ValueError):
                cmd_id_int = None
        else:
            cmd_id_int = None
    except Exception:
        cmd_id_int = None
        trigger = None

    # If this is a physical 'feeding' log produced by an actuator and it references a feed-now command,
    # merge it into the canonical 'feed_now' log (if present) so the UI sees one logical event.
    if str(log_type).strip().lower() == 'feeding' and cmd_id_int is not None:
        matched = Log.objects.filter(device=device, log_type='feed_now', payload__command_id=cmd_id_int).order_by('-id').first()
        if matched:
            # merge feeding payload into the feed_now record
            merged = dict(matched.payload or {})
            merged.update(payload or {})
            matched.payload = merged
            # prefer the physical feeding timestamp (more accurate)
            if log_timestamp is not None:
                matched.timestamp = log_timestamp
            matched.refresh_count += 1
            matched.save(update_fields=['timestamp', 'payload', 'refresh_count', 'last_updated'])
            return event, matched, False, True

    # When firmware reports the physical completion of a feed_now action, reconcile the
    # command row even if the explicit feed_now ack was not sent.
    if cmd_id_int is not None and trigger == 'feed_now':
        status_hint = str(
            (payload or {}).get('status')
            or (payload or {}).get('phase')
            or ((payload or {}).get('payload') or {}).get('status')
            or ((payload or {}).get('payload') or {}).get('phase')
            or ''
        ).strip().lower()
        resolved_status = FeedNowCommand.STATUS_FAILED if status_hint in {'failed', 'fail', 'error', 'timeout', 'cancelled', 'canceled'} else FeedNowCommand.STATUS_EXECUTED
        try:
            command = FeedNowCommand.objects.get(device=device, id=cmd_id_int)
        except FeedNowCommand.DoesNotExist:
            command = None
        if command is not None and command.status == FeedNowCommand.STATUS_PENDING:
            command.status = resolved_status
            command.executed_at = _normalize_dt(log_timestamp) or timezone.now()
            if resolved_status == FeedNowCommand.STATUS_FAILED:
                command.failure_reason = (status_hint or 'feeding log reported failure')[:255]
            else:
                command.failure_reason = ''
            command.save(update_fields=['status', 'executed_at', 'failure_reason', 'updated_at'])

    # If this is a 'feed_now' acknowledgement and there's an existing 'feeding' physical record,
    # merge the data into a canonical 'feed_now' record (create or update) so the UI shows a single row.
    if str(log_type).strip().lower() == 'feed_now' and cmd_id_int is not None:
        # prefer creating/updating the feed_now row as canonical
        feeding_match = Log.objects.filter(device=device, log_type='feeding', payload__command_id=cmd_id_int).order_by('-id').first()
        target_log = None
        # only reuse an existing feed_now row if feed_now is not considered stackable
        if existing_log and (existing_log.log_type == 'feed_now') and (not should_stack):
            target_log = existing_log

        if feeding_match and not target_log:
            # convert the existing feeding row into a feed_now canonical row
            feeding_match.log_type = 'feed_now'
            feeding_match.log_category = _derive_log_category('feed_now')
            merged = dict(feeding_match.payload or {})
            merged.update(payload or {})
            feeding_match.payload = merged
            feeding_match.timestamp = log_timestamp or feeding_match.timestamp
            feeding_match.refresh_count += 1
            feeding_match.save(update_fields=['log_type', 'log_category', 'timestamp', 'payload', 'refresh_count', 'last_updated'])
            return event, feeding_match, False, True

        if target_log:
            merged = dict(target_log.payload or {})
            merged.update(payload or {})
            target_log.payload = merged
            target_log.timestamp = log_timestamp or target_log.timestamp
            target_log.refresh_count += 1
            target_log.save(update_fields=['timestamp', 'payload', 'refresh_count', 'last_updated'])
            return event, target_log, False, True

    # Default path: non-stackable existing update or create new log
    if existing_log and not should_stack:
        existing_log.timestamp = log_timestamp
        existing_log.payload = payload
        existing_log.refresh_count += 1
        existing_log.save(update_fields=['timestamp', 'payload', 'refresh_count', 'last_updated'])
        return event, existing_log, False, True

    log = Log.objects.create(
        device=device,
        log_type=log_type,
        log_category=_derive_log_category(log_type),
        payload=payload,
        timestamp=log_timestamp,
    )
    return event, log, False, False


def _ingest_sensor_projection(
    device,
    feeder_level,
    water_level,
    timestamp,
    *,
    battery_voltage=None,
    feed_sufficient=None,
    feed_current_kg=None,
    feed_required_next_kg=None,
    event_id=None,
    boot_id='',
    sequence=None,
    source='esp32',
):
    payload = {
        'feeder_level_pct': feeder_level,
        'water_level_pct': water_level,
    }
    if battery_voltage is not None:
        payload['battery_voltage_v'] = battery_voltage
    if feed_sufficient is not None:
        payload['feed_sufficient'] = feed_sufficient
    if feed_current_kg is not None:
        payload['feed_current_kg'] = feed_current_kg
    if feed_required_next_kg is not None:
        payload['feed_required_next_kg'] = feed_required_next_kg

    event, created = _record_device_event(
        device=device,
        event_type='sensor_state',
        payload=payload,
        occurred_at=timestamp,
        event_id=event_id,
        boot_id=boot_id,
        sequence=sequence,
        source=source,
    )

    if not created:
        sensor_state = DeviceSensorState.objects.filter(device=device).first()
        if sensor_state is None:
            sensor_state = DeviceSensorState(device=device)
        return event, sensor_state, True

    sensor_state, _ = DeviceSensorState.objects.get_or_create(device=device)
    sensor_state.feeder_level_pct = feeder_level
    sensor_state.water_level_pct = water_level
    if battery_voltage is not None:
        try:
            sensor_state.battery_voltage_v = float(battery_voltage)
        except (TypeError, ValueError):
            pass
    if feed_sufficient is not None:
        sensor_state.feed_sufficient = bool(feed_sufficient)
    if feed_current_kg is not None:
        try:
            sensor_state.feed_current_kg = float(feed_current_kg)
        except (TypeError, ValueError):
            pass
    if feed_required_next_kg is not None:
        try:
            sensor_state.feed_required_next_kg = float(feed_required_next_kg)
        except (TypeError, ValueError):
            pass
    sensor_state.last_reported_at = timestamp or timezone.now()
    sensor_state.save()
    return event, sensor_state, False


def _ack_feed_now_command(device, command_id, status_value, reason=None, *, event_id=None, boot_id='', sequence=None, source='esp32'):
    payload = {
        'command_id': command_id,
        'status': status_value,
    }
    if reason:
        payload['reason'] = reason

    event, created = _record_device_event(
        device=device,
        event_type='feed_now_ack',
        payload=payload,
        occurred_at=timezone.now(),
        event_id=event_id,
        boot_id=boot_id,
        sequence=sequence,
        source=source,
    )

    command = get_object_or_404(FeedNowCommand, id=command_id, device=device)

    if command.status != FeedNowCommand.STATUS_PENDING:
        return event, command, True, False

    command.status = status_value
    command.executed_at = timezone.now()
    if status_value == FeedNowCommand.STATUS_FAILED:
        command.failure_reason = (reason or '')[:255]
    command.save(update_fields=['status', 'executed_at', 'failure_reason', 'updated_at'])
    return event, command, False, True


def _get_effective_config_last_updated(device, cfg):
    latest_schedule = (
        Schedule.objects.filter(device=device)
        .order_by('-last_updated')
        .values_list('last_updated', flat=True)
        .first()
    )
    latest_settings = SystemSettings.get_solo().updated_at
    latest_feed_command = (
        FeedNowCommand.objects.filter(device=device)
        .order_by('-updated_at')
        .values_list('updated_at', flat=True)
        .first()
    )

    effective = cfg.last_updated
    if latest_schedule and latest_schedule > effective:
        effective = latest_schedule
    if latest_settings and latest_settings > effective:
        effective = latest_settings
    if latest_feed_command and latest_feed_command > effective:
        effective = latest_feed_command
    return effective


def _serialize_feed_now_command(command):
    return {
        'id': command.id,
        'device': command.device_id,
        'amount_kg': command.amount_kg,
        'status': command.status,
        'created_at': command.created_at.isoformat(),
        'updated_at': command.updated_at.isoformat(),
        'requested_by': command.requested_by,
        'executed_at': command.executed_at.isoformat() if command.executed_at else None,
        'failure_reason': command.failure_reason,
    }


def _reconcile_feed_now_command_from_logs(command):
    if command.status != FeedNowCommand.STATUS_PENDING:
        return command, False

    matching_logs = (
        Log.objects.filter(
            device=command.device,
            log_type__in=['feeding', 'feed_now'],
        )
        .order_by('-timestamp', '-id')
    )

    for log in matching_logs:
        payload = log.payload if isinstance(log.payload, dict) else {}
        nested_payload = payload.get('payload') if isinstance(payload.get('payload'), dict) else {}

        candidate_command_id = payload.get('command_id') or nested_payload.get('command_id')
        try:
            candidate_command_id = int(candidate_command_id)
        except (TypeError, ValueError):
            continue

        if candidate_command_id != command.id:
            continue

        trigger = str(payload.get('trigger') or nested_payload.get('trigger') or nested_payload.get('event') or '').strip().lower()
        if log.log_type == 'feeding' and trigger != 'feed_now':
            continue

        status_hint = str(
            payload.get('status')
            or payload.get('phase')
            or nested_payload.get('status')
            or nested_payload.get('phase')
            or ''
        ).strip().lower()
        resolved_status = FeedNowCommand.STATUS_FAILED if status_hint in {'failed', 'fail', 'error', 'timeout', 'cancelled', 'canceled'} else FeedNowCommand.STATUS_EXECUTED

        command.status = resolved_status
        command.executed_at = _normalize_dt(log.timestamp) or timezone.now()
        if resolved_status == FeedNowCommand.STATUS_FAILED:
            command.failure_reason = (status_hint or 'feeding log reported failure')[:255]
        else:
            command.failure_reason = ''
        command.save(update_fields=['status', 'executed_at', 'failure_reason', 'updated_at'])
        return command, True

    return command, False


def _serialize_pending_feed_now_command(device):
    pending_commands = (
        FeedNowCommand.objects
        .filter(device=device, status=FeedNowCommand.STATUS_PENDING)
        .order_by('created_at', 'id')
    )
    for cmd in pending_commands:
        cmd, _ = _reconcile_feed_now_command_from_logs(cmd)
        if cmd.status == FeedNowCommand.STATUS_PENDING:
            return _serialize_feed_now_command(cmd)
    return None


def _with_runtime_time_fields(payload):
    now_utc = timezone.now()
    now_local = timezone.localtime(now_utc)
    data = dict(payload)
    data.update(_get_effective_max_capacity())
    data.update(_get_effective_thresholds())
    data.update(_get_effective_alert_thresholds())
    data.update(_get_effective_battery_shutdown_threshold())
    data['server_time_utc'] = now_utc.isoformat()
    data['server_time_local'] = now_local.isoformat()
    data['timezone_effective'] = _get_system_timezone_name()
    return data


def _get_important_log_keywords():
    settings_obj = SystemSettings.get_solo()
    keywords = settings_obj.important_log_keywords or []
    if not isinstance(keywords, list) or not keywords:
        return DEFAULT_IMPORTANT_LOG_KEYWORDS
    normalized = []
    for item in keywords:
        if isinstance(item, str) and item.strip():
            normalized.append(item.strip().lower())
    return normalized or DEFAULT_IMPORTANT_LOG_KEYWORDS


def _get_smtp_credentials():
    settings_obj = SystemSettings.get_solo()
    smtp_user = (settings.EMAIL_HOST_USER or settings_obj.smtp_email_user or '').strip()
    smtp_password = settings.EMAIL_HOST_PASSWORD or settings_obj.smtp_email_password or ''
    return smtp_user, smtp_password


def _coerce_notification_recipients(explicit_email=None):
    if explicit_email:
        validate_email(explicit_email)
        return [explicit_email]

    settings_obj = SystemSettings.get_solo()
    configured = settings_obj.alert_recipients or []
    if isinstance(configured, list):
        normalized = []
        for item in configured:
            if not isinstance(item, str):
                continue
            email = item.strip().lower()
            if not email:
                continue
            validate_email(email)
            normalized.append(email)

        deduped = list(dict.fromkeys(normalized))
        if deduped:
            return deduped

    recipients = list(
        User.objects.filter(is_active=True)
        .exclude(email='')
        .values_list('email', flat=True)
        .distinct()
    )
    return recipients


def _send_notification_email(subject, message, recipients):
    if not recipients:
        return {'ok': False, 'detail': 'No recipient emails configured'}

    configured_backend = getattr(settings, 'EMAIL_BACKEND', 'django.core.mail.backends.smtp.EmailBackend')
    backend_name = str(configured_backend or '').lower()
    smtp_user, smtp_password = _get_smtp_credentials()
    requires_smtp_credentials = 'smtp' in backend_name
    if requires_smtp_credentials and (not smtp_user or not smtp_password):
        return {'ok': False, 'detail': 'SMTP credentials are not configured'}

    try:
        if requires_smtp_credentials:
            connection = get_connection(
                backend=configured_backend,
                host=getattr(settings, 'EMAIL_HOST', 'smtp.gmail.com'),
                port=getattr(settings, 'EMAIL_PORT', 587),
                username=smtp_user,
                password=smtp_password,
                use_tls=getattr(settings, 'EMAIL_USE_TLS', True),
                timeout=getattr(settings, 'EMAIL_TIMEOUT', 20),
            )
        else:
            connection = get_connection(
                backend=configured_backend,
                timeout=getattr(settings, 'EMAIL_TIMEOUT', 20),
            )

        from_email = smtp_user or getattr(settings, 'DEFAULT_FROM_EMAIL', 'no-reply@stocklink.local')
        email = EmailMessage(
            subject=subject,
            body=message,
            from_email=from_email,
            to=recipients,
            connection=connection,
        )
        sent_count = email.send(fail_silently=False)
    except Exception as exc:
        logger.exception('Email send failed')
        return {'ok': False, 'detail': str(exc)}

    return {'ok': sent_count > 0, 'sent_count': sent_count}


def _is_important_log(log_type, payload):
    candidate = f"{log_type} {json.dumps(payload, sort_keys=True)}".lower()
    return any(keyword in candidate for keyword in _get_important_log_keywords())


def _send_alert_notification(alert, is_refresh):
    try:
        recipients = _coerce_notification_recipients()
    except ValidationError:
        return {'ok': False, 'detail': 'Invalid recipient email in user records'}

    event_label = 'updated' if is_refresh else 'triggered'
    subject = f"[StockLink Alert] {alert.alert_type} {event_label} on {alert.device.device_id}"
    message = (
        f"Device: {alert.device.device_id}\n"
        f"Alert type: {alert.alert_type}\n"
        f"Status: {'unresolved' if not alert.resolved else 'resolved'}\n"
        f"Occurrences: {alert.refresh_count}\n"
        f"Event time: {timezone.localtime(alert.timestamp).isoformat()}"
    )
    return _send_notification_email(subject, message, recipients)


def _send_important_log_notification(device, log_type, payload, log_timestamp, refresh_count):
    try:
        recipients = _coerce_notification_recipients()
    except ValidationError:
        return {'ok': False, 'detail': 'Invalid recipient email in user records'}

    subject = f"[StockLink Notification] Important log on {device.device_id}: {log_type}"
    message = (
        f"Device: {device.device_id}\n"
        f"Log type: {log_type}\n"
        f"Occurrences: {refresh_count}\n"
        f"Timestamp: {timezone.localtime(log_timestamp).isoformat()}\n\n"
        f"Payload:\n{json.dumps(payload, indent=2, sort_keys=True)}"
    )
    return _send_notification_email(subject, message, recipients)


def _build_verification_frontend_url(request, uidb64, token):
    frontend_url = (getattr(settings, 'FRONTEND_URL', '') or '').strip().rstrip('/')
    if frontend_url:
        return f"{frontend_url}/verify-email?uid={uidb64}&token={token}"
    return request.build_absolute_uri(f"/verify-email?uid={uidb64}&token={token}")


def _send_verification_email(request, user):
    uidb64 = urlsafe_base64_encode(force_bytes(user.pk))
    token = default_token_generator.make_token(user)
    verification_url = _build_verification_frontend_url(request, uidb64, token)
    subject = 'Verify your StockLink account email'
    message = (
        f"Hello {user.username},\n\n"
        "Please verify your email address to activate your StockLink account.\n\n"
        f"Verification link: {verification_url}\n\n"
        "If you did not create this account, you can ignore this email."
    )
    result = _send_notification_email(subject, message, [user.email])
    return result, uidb64, token


def _build_password_reset_frontend_url(request, uidb64, token):
    frontend_url = (getattr(settings, 'FRONTEND_URL', '') or '').strip().rstrip('/')
    if frontend_url:
        return f"{frontend_url}/reset-password?uid={uidb64}&token={token}"
    return request.build_absolute_uri(f"/reset-password?uid={uidb64}&token={token}")


def _send_password_reset_email(request, user):
    uidb64 = urlsafe_base64_encode(force_bytes(user.pk))
    token = default_token_generator.make_token(user)
    reset_url = _build_password_reset_frontend_url(request, uidb64, token)
    subject = 'Reset your StockLink account password'
    message = (
        f"Hello {user.username},\n\n"
        "We received a request to reset your StockLink password.\n\n"
        f"Reset link: {reset_url}\n\n"
        "If you did not request this, you can ignore this email."
    )
    result = _send_notification_email(subject, message, [user.email])
    return result, uidb64, token


def _send_user_notification_email(user, subject, message):
    recipient = (getattr(user, 'email', '') or '').strip().lower()
    if not recipient:
        return {'ok': False, 'detail': 'No recipient email configured'}

    try:
        validate_email(recipient)
    except ValidationError:
        return {'ok': False, 'detail': 'Invalid recipient email configured'}

    return _send_notification_email(subject, message, [recipient])


def _resend_key_for_email(email):
    return f"email_verify_resend:{email}"


def _serialize_user_access(user):
    is_authenticated = bool(user and user.is_authenticated)
    is_staff = bool(getattr(user, 'is_staff', False)) if is_authenticated else False
    approval = None
    try:
        approval = getattr(user, 'approval', None) if is_authenticated else None
    except Exception:
        approval = None
    is_approved = bool(getattr(approval, 'is_approved', False)) if approval is not None else False
    is_rejected = bool(getattr(approval, 'rejected', False)) if approval is not None else False
    approved_by = getattr(getattr(approval, 'approved_by', None), 'id', None) if approval is not None else None
    approved_at = getattr(approval, 'approved_at', None).isoformat() if (approval is not None and getattr(approval, 'approved_at', None) is not None) else None
    return {
        'id': getattr(user, 'id', None) if is_authenticated else None,
        'username': getattr(user, 'username', '') if is_authenticated else '',
        'email': getattr(user, 'email', '') if is_authenticated else '',
        'is_authenticated': is_authenticated,
        'is_staff': is_staff,
        'role': 'ADMIN' if is_staff else 'USER',
        'is_approved': is_approved,
        'is_rejected': is_rejected,
        'approved_by': approved_by,
        'approved_at': approved_at,
    }


def _serialize_admin_user(user):
    data = _serialize_user_access(user)
    data.update({
        'id': user.id,
        'is_active': user.is_active,
        'is_superuser': user.is_superuser,
    })
    try:
        appr = getattr(user, 'approval', None)
        if appr is not None:
            data.update({
                'approval': {
                    'is_approved': bool(appr.is_approved),
                    'rejected': bool(appr.rejected),
                    'rejected_reason': getattr(appr, 'rejected_reason', ''),
                    'approved_by': getattr(getattr(appr, 'approved_by', None), 'id', None),
                    'approved_at': getattr(appr, 'approved_at', None).isoformat() if getattr(appr, 'approved_at', None) is not None else None,
                }
            })
    except Exception:
        # best-effort only
        try:
            appr = getattr(user, 'approval', None)
            if appr is not None:
                data.update({'approval': {'is_approved': bool(appr.is_approved), 'rejected': bool(appr.rejected)}})
        except Exception:
            pass
    try:
        active_admin_count = User.objects.filter(is_staff=True, is_active=True).count()
        threshold = max(1, math.ceil(active_admin_count / 2))
        promote_votes = AdminRoleVote.objects.filter(target_user=user, vote_type=AdminRoleVote.VOTE_PROMOTE).count()
        demote_votes = AdminRoleVote.objects.filter(target_user=user, vote_type=AdminRoleVote.VOTE_DEMOTE).count()
        data['role_vote'] = {
            'active_admin_count': active_admin_count,
            'threshold': threshold,
            'promote_votes': promote_votes,
            'demote_votes': demote_votes,
            'vote_type': AdminRoleVote.VOTE_DEMOTE if user.is_staff else AdminRoleVote.VOTE_PROMOTE,
        }
    except Exception:
        data['role_vote'] = {
            'active_admin_count': 0,
            'threshold': 1,
            'promote_votes': 0,
            'demote_votes': 0,
            'vote_type': AdminRoleVote.VOTE_DEMOTE if user.is_staff else AdminRoleVote.VOTE_PROMOTE,
        }
    return data


def _build_admin_user_response(user):
    return Response(_serialize_admin_user(user))


def _deny_admin_role_change(request_user, target_user):
    if target_user.pk == request_user.pk:
        return Response({'detail': 'You cannot modify your own role.'}, status=status.HTTP_403_FORBIDDEN)
    if target_user.is_superuser:
        return Response({'detail': 'Superuser role changes are reserved for root administration.'}, status=status.HTTP_403_FORBIDDEN)
    return None


def _can_demote_staff_user(target_user):
    return User.objects.filter(is_staff=True).exclude(pk=target_user.pk).exists()


def _clear_role_votes_for_target(target_user):
    AdminRoleVote.objects.filter(target_user=target_user).delete()


def _role_vote_threshold():
    active_admin_count = User.objects.filter(is_staff=True, is_active=True).count()
    return max(1, math.ceil(active_admin_count / 2))


def _can_modify_admin_role(request_user, target_user):
    denial = _deny_admin_role_change(request_user, target_user)
    if denial is not None:
        return denial

    appr = getattr(target_user, 'approval', None)
    if not target_user.is_active or not (appr and getattr(appr, 'is_approved', False)):
        return Response({'detail': 'Account must be active and approved before role changes can be voted on.'}, status=status.HTTP_409_CONFLICT)

    return None


def _cast_role_vote(request_user, target_user, vote_type):
    denial = _can_modify_admin_role(request_user, target_user)
    if denial is not None:
        return denial

    if vote_type == AdminRoleVote.VOTE_PROMOTE and target_user.is_staff:
        return Response({'detail': 'User is already an admin.', 'user': _serialize_admin_user(target_user)}, status=status.HTTP_200_OK)

    if vote_type == AdminRoleVote.VOTE_DEMOTE and not target_user.is_staff:
        return Response({'detail': 'User is not currently an admin.', 'user': _serialize_admin_user(target_user)}, status=status.HTTP_200_OK)

    vote, created = AdminRoleVote.objects.get_or_create(target_user=target_user, voter=request_user, vote_type=vote_type)
    threshold = _role_vote_threshold()
    current_count = AdminRoleVote.objects.filter(target_user=target_user, vote_type=vote_type).count()

    if current_count < threshold:
        payload = _serialize_admin_user(target_user)
        payload.update({
            'detail': 'Vote recorded.',
            'vote_recorded': created,
            'vote_count': current_count,
            'vote_threshold': threshold,
            'vote_type': vote_type,
        })
        return Response(payload, status=status.HTTP_200_OK)

    if vote_type == AdminRoleVote.VOTE_DEMOTE and not _can_demote_staff_user(target_user):
        return Response({'detail': 'Cannot remove the last remaining admin account.'}, status=status.HTTP_409_CONFLICT)

    if vote_type == AdminRoleVote.VOTE_PROMOTE:
        target_user.is_staff = True
    else:
        target_user.is_staff = False
    target_user.save(update_fields=['is_staff'])
    _clear_role_votes_for_target(target_user)

    role_label = 'admin' if vote_type == AdminRoleVote.VOTE_PROMOTE else 'standard user'
    subject = f"[StockLink] Your account was {'promoted' if vote_type == AdminRoleVote.VOTE_PROMOTE else 'demoted'}"
    message = (
        f"Hello {target_user.username},\n\n"
        f"Your StockLink account has been {'promoted to admin access' if vote_type == AdminRoleVote.VOTE_PROMOTE else 'demoted from admin access'}.\n"
        f"Current access level: {role_label}.\n\n"
        f"Action taken by: {request_user.username}\n"
        "If you have questions about this change, please contact your system administrator."
    )
    _send_user_notification_email(target_user, subject, message)

    payload = _serialize_admin_user(target_user)
    payload.update({
        'detail': f"{'Promoted' if vote_type == AdminRoleVote.VOTE_PROMOTE else 'Demoted'} after reaching the vote threshold.",
        'vote_recorded': created,
        'vote_count': current_count,
        'vote_threshold': threshold,
        'vote_type': vote_type,
        'action_applied': True,
    })
    return Response(payload, status=status.HTTP_200_OK)


def _update_staff_role(request_user, target_user, make_staff):
    vote_type = AdminRoleVote.VOTE_PROMOTE if make_staff else AdminRoleVote.VOTE_DEMOTE
    return _cast_role_vote(request_user, target_user, vote_type)


def _deny_admin_approval_change(request_user, target_user):
    if target_user.pk == request_user.pk:
        return Response({'detail': 'You cannot modify your own approval.'}, status=status.HTTP_403_FORBIDDEN)
    if target_user.is_superuser:
        return Response({'detail': 'Superuser approval changes are reserved for root administration.'}, status=status.HTTP_403_FORBIDDEN)
    return None


def _update_user_approval(request_user, target_user, approve=True, reason=None):
    denial = _deny_admin_approval_change(request_user, target_user)
    if denial is not None:
        return denial

    appr, _ = UserApproval.objects.get_or_create(user=target_user)
    if approve:
        if appr.is_approved:
            return _build_admin_user_response(target_user)
        appr.is_approved = True
        appr.rejected = False
        appr.rejected_reason = ''
        appr.approved_by = request_user
        appr.approved_at = timezone.now()
        appr.save(update_fields=['is_approved', 'rejected', 'rejected_reason', 'approved_by', 'approved_at'])
        subject = '[StockLink] Your account has been approved'
        message = (
            f"Hello {target_user.username},\n\n"
            "Your StockLink account has been approved and you can now sign in.\n\n"
            f"Approved by: {request_user.username}\n"
            "You may now log in with your existing credentials."
        )
        _send_user_notification_email(target_user, subject, message)
        return _build_admin_user_response(target_user)

    appr.is_approved = False
    appr.rejected = True
    appr.rejected_reason = reason or ''
    appr.approved_by = None
    appr.approved_at = None
    appr.save(update_fields=['is_approved', 'rejected', 'rejected_reason', 'approved_by', 'approved_at'])

    subject = '[StockLink] Your account has been rejected'
    message_lines = [
        f"Hello {target_user.username},",
        '',
        'Your StockLink account has been rejected and was not approved for access.',
        f"Reviewed by: {request_user.username}",
    ]
    if reason:
        message_lines.extend(['', f"Reason: {reason}"])
    message_lines.extend(['', 'If you believe this was a mistake, please contact the system administrator.'])
    _send_user_notification_email(target_user, subject, '\n'.join(message_lines))

    target_user.delete()
    return Response({'detail': 'Account rejected and deleted.'}, status=status.HTTP_200_OK)


def _delete_admin_user(request_user, target_user):
    denial = _deny_admin_approval_change(request_user, target_user)
    if denial is not None:
        return denial

    if target_user.is_staff and not _can_demote_staff_user(target_user):
        return Response({'detail': 'Cannot delete the last remaining admin account.'}, status=status.HTTP_409_CONFLICT)

    username = target_user.username
    target_user.delete()
    return Response({'detail': f'Account deleted: {username}'}, status=status.HTTP_200_OK)


class ScheduleListCreateView(APIView):
    """List/create schedules for a device. Authenticated web user allowed; devices should not use this endpoint."""

    permission_classes = [IsAuthenticated]

    def get(self, request, device_id):
        device = get_object_or_404(Device, device_id=device_id)
        qs = Schedule.objects.filter(device=device).order_by('id')
        serializer = ScheduleSerializer(qs, many=True)
        return Response(serializer.data)

    def post(self, request, device_id):
        device = get_object_or_404(Device, device_id=device_id)
        data = request.data.copy()
        data['device'] = device.id
        serializer = ScheduleSerializer(data=data)
        if serializer.is_valid():
            serializer.save()
            _sync_deviceconfig_schedules(device)
            return Response(serializer.data, status=status.HTTP_201_CREATED)
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)


class ScheduleDetailView(APIView):
    permission_classes = [IsAuthenticated]

    def get(self, request, device_id, pk):
        device = get_object_or_404(Device, device_id=device_id)
        sched = get_object_or_404(Schedule, pk=pk, device=device)
        return Response(ScheduleSerializer(sched).data)

    def put(self, request, device_id, pk):
        device = get_object_or_404(Device, device_id=device_id)
        sched = get_object_or_404(Schedule, pk=pk, device=device)
        data = request.data.copy()
        data['device'] = device.id
        serializer = ScheduleSerializer(sched, data=data)
        if serializer.is_valid():
            serializer.save()
            _sync_deviceconfig_schedules(device)
            return Response(serializer.data)
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)

    def patch(self, request, device_id, pk):
        device = get_object_or_404(Device, device_id=device_id)
        sched = get_object_or_404(Schedule, pk=pk, device=device)
        data = request.data.copy()
        data['device'] = device.id
        serializer = ScheduleSerializer(sched, data=data, partial=True)
        if serializer.is_valid():
            serializer.save()
            _sync_deviceconfig_schedules(device)
            return Response(serializer.data)
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)

    def delete(self, request, device_id, pk):
        device = get_object_or_404(Device, device_id=device_id)
        sched = get_object_or_404(Schedule, pk=pk, device=device)
        sched.delete()
        _sync_deviceconfig_schedules(device)
        return Response(status=status.HTTP_204_NO_CONTENT)


class DeviceRegisterView(APIView):
    """Admin-only endpoint to register a device and return a generated token."""
    permission_classes = [IsAdminUser]

    def post(self, request):
        device_id = request.data.get('device_id')
        if not device_id:
            return Response({'detail': 'Missing device_id'}, status=status.HTTP_400_BAD_REQUEST)
        token = secrets.token_hex(16)
        device, created = Device.objects.get_or_create(device_id=device_id)
        device.auth_token = token
        device.save()
        return Response({'device_id': device.device_id, 'auth_token': device.auth_token})


class DeviceListView(APIView):
    """List known devices for authenticated web users."""

    permission_classes = [IsAuthenticated]

    def get(self, request):
        devices = Device.objects.order_by('device_id').values_list('device_id', flat=True)
        return Response({'devices': list(devices)})


class SystemSettingsView(APIView):
    """Get/update global system settings shared by frontend and devices."""

    permission_classes = [IsAdminUser]

    def get(self, request):
        settings_obj = SystemSettings.get_solo()
        serialized = SystemSettingsSerializer(settings_obj).data
        payload = _with_runtime_time_fields(serialized)
        payload['grain_type_index'] = _get_effective_grain_profile()['grain_type_index']
        payload['timezone_options'] = sorted(available_timezones())
        return Response(payload)

    def patch(self, request):
        settings_obj = SystemSettings.get_solo()
        serializer = SystemSettingsSerializer(settings_obj, data=request.data, partial=True)
        if serializer.is_valid():
            settings_obj._changed_system_setting_fields = set(serializer.validated_data.keys())
            serializer.save()
            if 'max_feeds_capacity_kg' in serializer.validated_data:
                _maybe_update_max_capacity(serializer.validated_data['max_feeds_capacity_kg'], 'server', timezone.now())
                serializer = SystemSettingsSerializer(SystemSettings.get_solo())
            payload = _with_runtime_time_fields(serializer.data)
            payload['grain_type_index'] = _get_effective_grain_profile()['grain_type_index']
            payload['timezone_options'] = sorted(available_timezones())
            return Response(payload)
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)


class RegisterUserView(APIView):
    """Public endpoint to create the single frontend user account."""

    def post(self, request):
        username = request.data.get('username')
        first_name = (request.data.get('first_name') or '').strip()
        last_name = (request.data.get('last_name') or '').strip()
        password = request.data.get('password')
        email = (request.data.get('email') or '').strip().lower()
        if not username or not password or not email:
            return Response({'detail': 'username, password, and email are required'}, status=status.HTTP_400_BAD_REQUEST)

        try:
            validate_email(email)
        except ValidationError:
            return Response({'detail': 'invalid email'}, status=status.HTTP_400_BAD_REQUEST)

        if User.objects.filter(username=username).exists():
            return Response({'detail': 'username already exists'}, status=status.HTTP_400_BAD_REQUEST)
        if User.objects.filter(email=email).exists():
            return Response({'detail': 'email already exists'}, status=status.HTTP_400_BAD_REQUEST)

        user = User.objects.create_user(
            username=username,
            password=password,
            email=email,
            first_name=first_name,
            last_name=last_name,
            is_active=False,
        )
        try:
            UserApproval.objects.get_or_create(user=user)
        except Exception:
            # best-effort: if approval cannot be created, continue (will be handled later)
            pass
        send_result, _, _ = _send_verification_email(request, user)

        if not send_result.get('ok'):
            user.delete()
            return Response(
                {'detail': f"Registration failed: {send_result.get('detail', 'Failed to send verification email')}"},
                status=status.HTTP_400_BAD_REQUEST,
            )

        return Response(
            {
                'username': user.username,
                'email': user.email,
                'first_name': user.first_name,
                'last_name': user.last_name,
                'detail': 'Verification email sent. Please check your inbox.',
            },
            status=status.HTTP_201_CREATED,
        )


class VerifyEmailView(APIView):
    """Public endpoint to verify a newly-registered account email."""

    def _verify(self, uidb64, token):
        if not uidb64 or not token:
            return Response({'detail': 'uid and token are required'}, status=status.HTTP_400_BAD_REQUEST)

        try:
            uid = force_str(urlsafe_base64_decode(uidb64))
            user = User.objects.get(pk=uid)
        except Exception:
            return Response({'detail': 'Invalid verification link'}, status=status.HTTP_400_BAD_REQUEST)

        if user.is_active:
            return Response({'detail': 'Email already verified'}, status=status.HTTP_200_OK)

        if not default_token_generator.check_token(user, token):
            return Response({'detail': 'Invalid or expired verification link'}, status=status.HTTP_400_BAD_REQUEST)

        user.is_active = True
        user.save(update_fields=['is_active'])
        cache.delete(_resend_key_for_email(user.email.lower()))
        return Response({'detail': 'Email verified successfully'}, status=status.HTTP_200_OK)

    def get(self, request):
        uidb64 = request.query_params.get('uid')
        token = request.query_params.get('token')
        return self._verify(uidb64, token)

    def post(self, request):
        uidb64 = request.data.get('uid')
        token = request.data.get('token')
        return self._verify(uidb64, token)


class ResendVerificationEmailView(APIView):
    """Public endpoint to resend verification emails with basic cooldown."""

    def post(self, request):
        email = (request.data.get('email') or '').strip().lower()
        if not email:
            return Response({'detail': 'email is required'}, status=status.HTTP_400_BAD_REQUEST)

        try:
            validate_email(email)
        except ValidationError:
            return Response({'detail': 'invalid email'}, status=status.HTTP_400_BAD_REQUEST)

        user = User.objects.filter(email=email).first()
        if user is None:
            return Response({'detail': 'If this email exists, a verification email was sent.'}, status=status.HTTP_200_OK)

        if user.is_active:
            return Response({'detail': 'Email already verified.'}, status=status.HTTP_200_OK)

        resend_key = _resend_key_for_email(email)
        if cache.get(resend_key):
            return Response(
                {'detail': f'Please wait {EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS} seconds before requesting another email.'},
                status=status.HTTP_429_TOO_MANY_REQUESTS,
            )

        send_result, _, _ = _send_verification_email(request, user)
        if not send_result.get('ok'):
            return Response(
                {'detail': send_result.get('detail', 'Failed to send verification email')},
                status=status.HTTP_400_BAD_REQUEST,
            )

        cache.set(resend_key, True, EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS)
        return Response({'detail': 'Verification email sent.'}, status=status.HTTP_200_OK)


class PasswordResetRequestView(APIView):
    """Public endpoint to send a password reset email for an existing account."""

    def post(self, request):
        email = (request.data.get('email') or '').strip().lower()
        if not email:
            return Response(
                {
                    'detail': 'Email address is required.',
                    'account_found': False,
                    'email_sent': False,
                },
                status=status.HTTP_400_BAD_REQUEST,
            )

        try:
            validate_email(email)
        except ValidationError:
            return Response({'detail': 'invalid email', 'account_found': False, 'email_sent': False}, status=status.HTTP_400_BAD_REQUEST)

        user = User.objects.filter(email=email, is_active=True).first()
        if user is not None:
            send_result, _, _ = _send_password_reset_email(request, user)
            if not send_result.get('ok'):
                return Response(
                    {
                        'detail': send_result.get('detail', 'Failed to send password reset email'),
                        'account_found': True,
                        'email_sent': False,
                    },
                    status=status.HTTP_400_BAD_REQUEST,
                )

        if user is not None:
            return Response(
                {
                    'detail': 'Password reset email sent.',
                    'account_found': True,
                    'email_sent': True,
                },
                status=status.HTTP_200_OK,
            )

        return Response(
            {
                'detail': 'No account found for that email.',
                'account_found': False,
                'email_sent': False,
            },
            status=status.HTTP_200_OK,
        )


class PasswordResetConfirmView(APIView):
    """Public endpoint to confirm a password reset token and set a new password."""

    def post(self, request):
        uidb64 = request.data.get('uid')
        token = request.data.get('token')
        new_password = request.data.get('new_password') or request.data.get('password') or ''
        confirm_password = request.data.get('confirm_password') or ''

        if not uidb64 or not token:
            return Response({'detail': 'uid and token are required'}, status=status.HTTP_400_BAD_REQUEST)

        if not new_password or not confirm_password:
            return Response({'detail': 'new password and confirmation are required'}, status=status.HTTP_400_BAD_REQUEST)

        if new_password != confirm_password:
            return Response({'detail': 'new passwords do not match'}, status=status.HTTP_400_BAD_REQUEST)

        try:
            uid = force_str(urlsafe_base64_decode(uidb64))
            user = User.objects.get(pk=uid, is_active=True)
        except Exception:
            return Response({'detail': 'Invalid or expired reset link'}, status=status.HTTP_400_BAD_REQUEST)

        if not default_token_generator.check_token(user, token):
            return Response({'detail': 'Invalid or expired reset link'}, status=status.HTTP_400_BAD_REQUEST)

        try:
            validate_password(new_password, user=user)
        except ValidationError as exc:
            message = exc.messages[0] if getattr(exc, 'messages', None) else 'invalid password'
            return Response({'detail': message}, status=status.HTTP_400_BAD_REQUEST)

        user.set_password(new_password)
        user.save(update_fields=['password'])

        if request.user and request.user.is_authenticated and request.user.pk == user.pk:
            update_session_auth_hash(request, user)

        return Response({'detail': 'Password reset successfully.'}, status=status.HTTP_200_OK)


class TestEmailView(APIView):
    """Authenticated endpoint to validate Gmail SMTP settings and delivery."""

    permission_classes = [IsAdminUser]

    def post(self, request):
        override_email = (request.data.get('email') or '').strip().lower() or None
        subject = request.data.get('subject') or 'StockLink test email'
        message = request.data.get('message') or 'This is a test email from StockLink SMTP integration.'

        try:
            recipients = _coerce_notification_recipients(override_email)
        except ValidationError:
            return Response({'detail': 'Invalid email format'}, status=status.HTTP_400_BAD_REQUEST)

        result = _send_notification_email(subject, message, recipients)
        if result.get('ok'):
            return Response(
                {
                    'detail': 'Test email sent',
                    'recipients': recipients,
                    'sent_count': result.get('sent_count', 0),
                },
                status=status.HTTP_200_OK,
            )
        return Response(
            {
                'detail': result.get('detail', 'Failed to send email'),
                'recipients': recipients,
            },
            status=status.HTTP_400_BAD_REQUEST,
        )


class LoginUserView(APIView):
    def post(self, request):
        from django.contrib.auth import authenticate, login
        identity = (request.data.get('username') or request.data.get('identity') or '').strip()
        password = request.data.get('password')
        if not identity or not password:
            return Response({'detail': 'username and password required'}, status=status.HTTP_400_BAD_REQUEST)

        lookup = User.objects.filter(username__iexact=identity).first()
        if lookup is None and '@' in identity:
            lookup = User.objects.filter(email__iexact=identity).first()

        if lookup is not None and not lookup.is_active and lookup.check_password(password):
            return Response(
                {'detail': 'Email not verified. Please verify your email before logging in.'},
                status=status.HTTP_403_FORBIDDEN,
            )

        # If email verified but awaiting admin approval, refuse login with clear message
        if lookup is not None and lookup.is_active and lookup.check_password(password):
            try:
                appr = getattr(lookup, 'approval', None)
                if appr is None:
                    # no approval record -> treat as pending
                    return Response({'detail': 'Awaiting admin approval. Your account will be enabled after a staff member approves it.'}, status=status.HTTP_403_FORBIDDEN)
                if not getattr(appr, 'is_approved', False):
                    return Response({'detail': 'Awaiting admin approval. Your account will be enabled after a staff member approves it.'}, status=status.HTTP_403_FORBIDDEN)
            except Exception:
                # On any error, be conservative and deny login
                return Response({'detail': 'Awaiting admin approval.'}, status=status.HTTP_403_FORBIDDEN)

        auth_username = lookup.username if lookup is not None else identity
        user = authenticate(request, username=auth_username, password=password)
        if user is None:
            return Response({'detail': 'invalid credentials'}, status=status.HTTP_401_UNAUTHORIZED)
        login(request, user)
        response_data = _serialize_user_access(user)
        return Response(response_data)


class LogoutUserView(APIView):
    def post(self, request):
        from django.contrib.auth import logout
        logout(request)
        return Response({'detail': 'logged out'})


class CurrentUserView(APIView):
    """Return brief info about the currently authenticated web user."""

    def get(self, request):
        return Response(_serialize_user_access(request.user))

    def patch(self, request):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)

        email = (request.data.get('email') or '').strip().lower()
        current_password = request.data.get('current_password') or ''
        new_password = request.data.get('new_password') or ''
        confirm_password = request.data.get('confirm_password') or ''

        update_fields = []
        response_data = {'username': request.user.username, 'email': request.user.email}

        if email:
            try:
                validate_email(email)
            except ValidationError:
                return Response({'detail': 'invalid email'}, status=status.HTTP_400_BAD_REQUEST)

            if User.objects.exclude(pk=request.user.pk).filter(email=email).exists():
                return Response({'detail': 'email already exists'}, status=status.HTTP_400_BAD_REQUEST)

            request.user.email = email
            update_fields.append('email')
            response_data['email'] = request.user.email

        password_fields_provided = any([current_password, new_password, confirm_password])
        if password_fields_provided:
            if not current_password or not new_password or not confirm_password:
                return Response({'detail': 'current password, new password, and confirmation are required'}, status=status.HTTP_400_BAD_REQUEST)

            if not request.user.check_password(current_password):
                return Response({'detail': 'current password is incorrect'}, status=status.HTTP_400_BAD_REQUEST)

            if new_password != confirm_password:
                return Response({'detail': 'new passwords do not match'}, status=status.HTTP_400_BAD_REQUEST)

            try:
                validate_password(new_password, user=request.user)
            except ValidationError as exc:
                message = exc.messages[0] if getattr(exc, 'messages', None) else 'invalid password'
                return Response({'detail': message}, status=status.HTTP_400_BAD_REQUEST)

            request.user.set_password(new_password)
            update_session_auth_hash(request, request.user)
            update_fields.append('password')
            response_data['detail'] = 'password updated'

        if not update_fields:
            return Response({'detail': 'email or password update is required'}, status=status.HTTP_400_BAD_REQUEST)

        request.user.save()
        return Response(response_data)

    def delete(self, request):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)

        from django.contrib.auth import logout

        user = request.user
        user.delete()
        logout(request)
        return Response({'detail': 'account deleted'})


class AdminUserListView(APIView):
    permission_classes = [IsAdminUser]

    def get(self, request):
        users = User.objects.order_by('username', 'id')
        return Response([_serialize_admin_user(user) for user in users])


class AdminUserPromoteView(APIView):
    permission_classes = [IsAdminUser]

    def post(self, request, user_id):
        target_user = get_object_or_404(User, pk=user_id)
        return _update_staff_role(request.user, target_user, make_staff=True)


class AdminUserDemoteView(APIView):
    permission_classes = [IsAdminUser]

    def post(self, request, user_id):
        target_user = get_object_or_404(User, pk=user_id)
        return _update_staff_role(request.user, target_user, make_staff=False)


class AdminUserApproveView(APIView):
    permission_classes = [IsAdminUser]

    def post(self, request, user_id):
        target_user = get_object_or_404(User, pk=user_id)
        return _update_user_approval(request.user, target_user, approve=True)


class AdminUserRejectView(APIView):
    permission_classes = [IsAdminUser]

    def post(self, request, user_id):
        target_user = get_object_or_404(User, pk=user_id)
        reason = (request.data.get('reason') or '').strip()
        return _update_user_approval(request.user, target_user, approve=False, reason=reason)


class AdminUserDeleteView(APIView):
    permission_classes = [IsAdminUser]

    def delete(self, request, user_id):
        target_user = get_object_or_404(User, pk=user_id)
        return _delete_admin_user(request.user, target_user)


def authorize_device(request, device_id):
    """Check `Authorization: Token <token>` header and return Device or None."""
    auth = request.META.get('HTTP_AUTHORIZATION', '')
    if auth.startswith('Token '):
        token = auth.split(' ', 1)[1]
        try:
            device = Device.objects.get(device_id=device_id, auth_token=token)
            return device
        except Device.DoesNotExist:
            return None
    return None


class DeviceConfigView(APIView):
    """GET returns server config; POST accepts device pushes or server management updates.

    Device pushes must authenticate with `Authorization: Token <token>` and include
    `config` and `last_updated` (ISO8601 UTC). Server validates the incoming timestamp
    against the server copy and will reject (409) if the server copy is newer.

    Server (management) pushes must be authenticated as staff/admin (session/basic auth)
    and will overwrite the device config setting `updated_by='server'`.
    """

    def get(self, request, device_id):
        device = authorize_device(request, device_id)
        if device is None:
            if not (request.user and request.user.is_authenticated):
                return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
            device = get_object_or_404(Device, device_id=device_id)
        cfg, _ = DeviceConfig.objects.get_or_create(device=device)
        serializer = DeviceConfigSerializer(cfg)
        data = serializer.data
        effective_last_updated = _get_effective_config_last_updated(device, cfg)
        data['last_updated'] = effective_last_updated.isoformat()
        data['config'] = data.get('config') or {}
        # Always derive schedules from canonical Schedule rows.
        data['config']['schedules'] = _serialize_device_schedules(device)
        data['config']['system_timezone'] = _get_system_timezone_name()
        data['config'].update(_get_effective_max_capacity())
        # Include device max single-feed ceiling
        try:
            data['config']['max_single_feed_kg'] = _compute_max_single_feed_kg(device, cfg=cfg)
        except Exception:
            pass
        device_grain_type = data['config'].get('grain_type')
        device_grain_type_index = data['config'].get('grain_type_index')
        _apply_effective_grain_config(
            data['config'],
            grain_types=data['config'].get('grain_types') or _get_effective_grain_types()['grain_types'],
            grain_type=device_grain_type if device_grain_type is not None else SystemSettings.get_effective().grain_type,
            grain_type_index=device_grain_type_index,
        )
        data['config'].update(_get_effective_thresholds())
        data['config'].update(_get_effective_alert_thresholds())
        data['config'].setdefault('low_battery_shutdown_v', _get_effective_battery_shutdown_threshold()['low_battery_shutdown_v'])
        data['config'].setdefault('total_feeds_today_kg', 0.0)
        data['config']['last_updated'] = data['last_updated']
        data['config']['updated_by'] = data.get('updated_by') or 'server'
        data['config']['feed_now_command'] = _serialize_pending_feed_now_command(device)
        # Include current sensor state
        data['sensor_state'] = _get_device_sensor_state(device)
        return Response(_with_runtime_time_fields(data))

    def post(self, request, device_id):
        # Try device auth first
        device = authorize_device(request, device_id)

        # Server (management) push via authenticated web user (allow single frontend user)
        if device is None and request.user and request.user.is_authenticated:
            if not request.user.is_staff:
                return Response({'detail': 'Forbidden'}, status=status.HTTP_403_FORBIDDEN)
            # Authenticated web user is pushing config to device
            device = get_object_or_404(Device, device_id=device_id)
            payload = request.data.get('config')
            if payload is None:
                return Response({'detail': 'Missing config'}, status=status.HTTP_400_BAD_REQUEST)

            candidate_dt = _parse_optional_dt(payload.get('max_feeds_capacity_updated_at')) or timezone.now()
            _maybe_update_max_capacity(payload.get('max_feeds_capacity_kg'), 'server', candidate_dt)
            _maybe_update_system_grain_type(payload.get('grain_type'), payload.get('grain_type_index'))
            sync_thresholds_from_payload(payload)
            sync_schedules_from_payload(device, payload)

            cfg, _ = DeviceConfig.objects.get_or_create(device=device)
            cfg.config = _merge_device_config(cfg.config or {}, payload)
            cfg.config['schedules'] = _serialize_device_schedules(device)
            cfg.config['system_timezone'] = _get_system_timezone_name()
            cfg.config.update(_get_effective_max_capacity())
            _apply_effective_grain_config(
                cfg.config,
                grain_types=cfg.config.get('grain_types') or _get_effective_grain_types()['grain_types'],
                grain_type=payload.get('grain_type') if payload.get('grain_type') is not None else cfg.config.get('grain_type'),
                grain_type_index=payload.get('grain_type_index') if payload.get('grain_type_index') is not None else cfg.config.get('grain_type_index'),
            )
            cfg.config.update(_get_effective_thresholds())
            cfg.config.update(_get_effective_alert_thresholds())
            cfg.config.setdefault('low_battery_shutdown_v', _get_effective_battery_shutdown_threshold()['low_battery_shutdown_v'])
            cfg.config.setdefault('total_feeds_today_kg', 0.0)
            # Ensure max_single_feed_kg stored (prefer payload/device value)
            try:
                cfg.config.setdefault('max_single_feed_kg', _compute_max_single_feed_kg(device, cfg=cfg))
            except Exception:
                pass
            cfg.updated_by = 'server'
            cfg.last_updated = timezone.now()
            cfg.save()
            return Response(_with_runtime_time_fields(DeviceConfigSerializer(cfg).data))

        # If still no device -> unauthorized
        if device is None:
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)

        # Device is authenticated -> expected payload
        payload = request.data.get('config')
        incoming_ts = request.data.get('last_updated')
        if payload is None or incoming_ts is None:
            return Response({'detail': 'Missing config or last_updated'}, status=status.HTTP_400_BAD_REQUEST)

        # Parse incoming timestamp
        inc_dt = parse_datetime(incoming_ts)
        if inc_dt is None:
            return Response({'detail': 'Invalid last_updated timestamp'}, status=status.HTTP_400_BAD_REQUEST)
        inc_dt = _normalize_dt(inc_dt)

        cfg, created = DeviceConfig.objects.get_or_create(device=device)
        # compare server copy timestamp
        server_ts = cfg.last_updated
        # normalize timezone-awareness
        if server_ts is not None and inc_dt <= server_ts:
            # Server has newer or equal -> reject and return current server copy
            return Response({'detail': 'Server copy is newer', 'server_config': DeviceConfigSerializer(cfg).data}, status=status.HTTP_409_CONFLICT)

        # Accept device config
        candidate_max_dt = _parse_optional_dt(payload.get('max_feeds_capacity_updated_at')) or inc_dt
        _maybe_update_max_capacity(payload.get('max_feeds_capacity_kg'), 'esp32', candidate_max_dt)
        sync_thresholds_from_payload(payload)
        sync_schedules_from_payload(device, payload)

        cfg.config = _merge_device_config(cfg.config or {}, payload)
        cfg.config['schedules'] = _serialize_device_schedules(device)
        cfg.config['system_timezone'] = _get_system_timezone_name()
        cfg.config.update(_get_effective_max_capacity())
        grain_types = cfg.config.get('grain_types') or _get_effective_grain_types()['grain_types']
        grain_type = payload.get('grain_type') if payload.get('grain_type') is not None else cfg.config.get('grain_type')
        grain_type_index = payload.get('grain_type_index') if payload.get('grain_type_index') is not None else cfg.config.get('grain_type_index')
        # Device UI may send updated grain_type while grain_type_index is stale; trust the name on conflict.
        if _grain_type_index_conflicts(grain_types, grain_type, grain_type_index):
            cfg.config.pop('grain_type_index', None)
            grain_type_index = None
        _apply_effective_grain_config(
            cfg.config,
            grain_types=grain_types,
            grain_type=grain_type,
            grain_type_index=grain_type_index,
        )
        cfg.config.update(_get_effective_thresholds())
        cfg.config.update(_get_effective_alert_thresholds())
        cfg.config.setdefault('low_battery_shutdown_v', _get_effective_battery_shutdown_threshold()['low_battery_shutdown_v'])
        cfg.config.setdefault('total_feeds_today_kg', 0.0)
        # Preserve device-provided max_single_feed_kg, otherwise compute and store one.
        try:
            cfg.config.setdefault('max_single_feed_kg', _compute_max_single_feed_kg(device, cfg=cfg))
        except Exception:
            pass
        cfg.updated_by = 'esp32'
        cfg.last_updated = inc_dt if inc_dt.tzinfo else timezone.make_aware(inc_dt)
        cfg.save()
        return Response(_with_runtime_time_fields(DeviceConfigSerializer(cfg).data))

class LogsView(APIView):
    def get(self, request, device_id):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        device = get_object_or_404(Device, device_id=device_id)
        _resolve_stale_shutdown_alerts(device=device)
        logs = Log.objects.filter(device=device).order_by('-timestamp')
        serializer = LogSerializer(logs, many=True)
        return Response(serializer.data)

    def post(self, request, device_id):
        device = authorize_device(request, device_id)
        if device is None:
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        log_type = str(request.data.get('log_type', 'generic')).strip().lower() or 'generic'
        payload = request.data.get('payload', {})
        if not isinstance(payload, dict):
            payload = {}

        timestamp_str = request.data.get('timestamp')
        if not timestamp_str:
            return Response({'detail': 'Missing timestamp'}, status=status.HTTP_400_BAD_REQUEST)

        log_timestamp = _parse_optional_dt(timestamp_str)
        if log_timestamp is None:
            return Response({'detail': 'Invalid timestamp format'}, status=status.HTTP_400_BAD_REQUEST)

        event_id = request.data.get('event_id') or request.data.get('request_id') or payload.get('event_id') or payload.get('request_id')
        boot_id = str(request.data.get('boot_id') or payload.get('boot_id') or '').strip()
        sequence = _coerce_event_sequence(request.data.get('sequence', payload.get('sequence')))
        source = str(request.data.get('source') or payload.get('source') or 'esp32').strip() or 'esp32'

        _mark_device_connection_seen(device, timezone.now(), trigger=log_type, source=source, extra_payload={'log_type': log_type})

        event, log, duplicate, refreshed = _ingest_log_projection(
            device=device,
            log_type=log_type,
            payload=payload,
            log_timestamp=log_timestamp,
            event_id=event_id,
            boot_id=boot_id,
            sequence=sequence,
            source=source,
        )

        if not duplicate and log_type in {LOW_BATTERY_SHUTDOWN_LOG_TYPE, LOW_BATTERY_SHUTDOWN_ALERT_TYPE}:
            _create_or_refresh_shutdown_alert(
                device=device,
                timestamp=log_timestamp,
                payload=payload,
                alert_type=LOW_BATTERY_SHUTDOWN_ALERT_TYPE,
                log_type=LOW_BATTERY_SHUTDOWN_LOG_TYPE,
                create_log=False,
            )

        email_result = None
        if not duplicate and not refreshed and _is_important_log(log_type, payload):
            email_result = _send_important_log_notification(
                device=device,
                log_type=log_type,
                payload=payload,
                log_timestamp=log_timestamp,
                refresh_count=log.refresh_count if log else 1,
            )

        response_id = getattr(log, 'id', None) or event.id
        return Response(
            {
                'id': response_id,
                'event_id': event.event_id,
                'duplicate': duplicate,
                'refreshed': refreshed,
                'email': email_result,
            },
            status=status.HTTP_200_OK if duplicate or refreshed else status.HTTP_201_CREATED,
        )

class AlertsView(APIView):
    def get(self, request, device_id):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        device = get_object_or_404(Device, device_id=device_id)
        _resolve_stale_shutdown_alerts(device=device)
        alerts = Alert.objects.filter(device=device).order_by('-last_updated', '-id')[:200]
        serializer = AlertSerializer(alerts, many=True)
        return Response(serializer.data)

    def post(self, request, device_id):
        device = authorize_device(request, device_id)
        if device is None:
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        alert_type = str(request.data.get('alert_type') or '').strip().lower()
        if not alert_type:
            return Response({'detail': 'Missing alert_type'}, status=status.HTTP_400_BAD_REQUEST)

        now_ts = timezone.now()

        if alert_type == 'power_outage':
            existing_outage = Alert.objects.filter(
                device=device,
                alert_type='power_outage',
                resolved=False,
            ).order_by('-last_updated', '-id').first()

            if existing_outage:
                age_seconds = (now_ts - existing_outage.last_updated).total_seconds()
                if age_seconds <= POWER_ALERT_DEDUP_SECONDS:
                    return Response(
                        {
                            'id': existing_outage.id,
                            'refreshed': True,
                            'deduped': True,
                            'detail': 'Duplicate power_outage ignored within debounce window',
                            'email': None,
                        },
                        status=status.HTTP_200_OK,
                    )

                existing_outage.timestamp = now_ts
                existing_outage.refresh_count += 1
                existing_outage.save(update_fields=['timestamp', 'refresh_count', 'last_updated'])
                # Also update or create a corresponding log entry for power outage
                try:
                    existing_log = Log.objects.filter(device=device, log_type='power_outage').order_by('-id').first()
                    if existing_log:
                        existing_log.timestamp = now_ts
                        existing_log.refresh_count += 1
                        existing_log.payload = existing_log.payload or {}
                        existing_log.payload.update({'note': 'power_outage refreshed'})
                        existing_log.save(update_fields=['timestamp', 'payload', 'refresh_count', 'last_updated'])
                    else:
                        Log.objects.create(
                            device=device,
                            log_type='power_outage',
                            log_category=_derive_log_category('power_outage'),
                            payload={'note': 'power outage detected'},
                            timestamp=now_ts,
                        )
                except Exception:
                    logger.exception('Failed to create/update power_outage log')
                return Response(
                    {
                        'id': existing_outage.id,
                        'refreshed': True,
                        'deduped': False,
                        'email': None,
                    },
                    status=status.HTTP_200_OK,
                )

            outage = Alert.objects.create(device=device, alert_type='power_outage', timestamp=now_ts)
            # Create a corresponding log entry for the outage so it appears in the logs view
            try:
                Log.objects.create(
                    device=device,
                    log_type='power_outage',
                    log_category=_derive_log_category('power_outage'),
                    payload={'alert_id': outage.id, 'note': 'power outage detected'},
                    timestamp=now_ts,
                )
            except Exception:
                logger.exception('Failed to create power_outage log')

            email_result = _send_alert_notification(outage, is_refresh=False)
            return Response(
                {
                    'id': outage.id,
                    'refreshed': False,
                    'deduped': False,
                    'email': email_result,
                },
                status=status.HTTP_201_CREATED,
            )

        if alert_type == LOW_BATTERY_SHUTDOWN_ALERT_TYPE:
            payload = request.data.get('payload', {})
            if not isinstance(payload, dict):
                payload = {}

            alert, refreshed, deduped, email_result = _create_or_refresh_shutdown_alert(
                device=device,
                timestamp=now_ts,
                payload=payload,
                alert_type=LOW_BATTERY_SHUTDOWN_ALERT_TYPE,
                log_type=LOW_BATTERY_SHUTDOWN_LOG_TYPE,
                create_log=True,
            )
            return Response(
                {
                    'id': alert.id,
                    'refreshed': refreshed,
                    'deduped': deduped,
                    'email': email_result,
                },
                status=status.HTTP_200_OK if refreshed or deduped else status.HTTP_201_CREATED,
            )

        if alert_type == LOW_BATTERY_SHUTDOWN_RESOLVED_ALERT_TYPE:
            payload = request.data.get('payload', {})
            if not isinstance(payload, dict):
                payload = {}

            active_alert = Alert.objects.filter(
                device=device,
                alert_type=LOW_BATTERY_SHUTDOWN_ALERT_TYPE,
                resolved=False,
            ).order_by('-last_updated', '-id').first()

            if active_alert:
                active_alert.resolved = True
                active_alert.payload = dict(active_alert.payload or {})
                active_alert.payload.update(payload)
                active_alert.save(update_fields=['resolved', 'payload', 'last_updated'])

            resolved_alert = Alert.objects.create(
                device=device,
                alert_type=LOW_BATTERY_SHUTDOWN_RESOLVED_ALERT_TYPE,
                timestamp=now_ts,
                resolved=True,
                payload=payload,
            )
            try:
                _ingest_log_projection(
                    device=device,
                    log_type=f'{LOW_BATTERY_SHUTDOWN_LOG_TYPE}_resolved',
                    payload=payload,
                    log_timestamp=now_ts,
                    source='server',
                )
            except Exception:
                logger.exception('Failed to create low battery shutdown resolved log')
            return Response(
                {
                    'id': resolved_alert.id,
                    'refreshed': False,
                    'deduped': False,
                    'resolved_shutdown_alerts': 1 if active_alert else 0,
                    'email': None,
                },
                status=status.HTTP_201_CREATED,
            )

        if alert_type == 'power_restored':
            existing_restored = Alert.objects.filter(
                device=device,
                alert_type='power_restored',
            ).order_by('-last_updated', '-id').first()

            if existing_restored:
                age_seconds = (now_ts - existing_restored.last_updated).total_seconds()
                if age_seconds <= POWER_ALERT_DEDUP_SECONDS:
                    resolved_count = Alert.objects.filter(
                        device=device,
                        alert_type='power_outage',
                        resolved=False,
                    ).update(resolved=True, last_updated=now_ts)
                    return Response(
                        {
                            'id': existing_restored.id,
                            'refreshed': True,
                            'deduped': True,
                            'resolved_outage_alerts': resolved_count,
                            'detail': 'Duplicate power_restored ignored within debounce window',
                            'email': None,
                        },
                        status=status.HTTP_200_OK,
                    )

            resolved_count = Alert.objects.filter(
                device=device,
                alert_type='power_outage',
                resolved=False,
            ).update(resolved=True, last_updated=now_ts)

            if existing_restored:
                existing_restored.timestamp = now_ts
                existing_restored.refresh_count += 1
                existing_restored.resolved = True
                existing_restored.save(update_fields=['timestamp', 'refresh_count', 'resolved', 'last_updated'])
                return Response(
                    {
                        'id': existing_restored.id,
                        'refreshed': True,
                        'deduped': False,
                        'resolved_outage_alerts': resolved_count,
                        'email': None,
                    },
                    status=status.HTTP_200_OK,
                )

            restored = Alert.objects.create(
                device=device,
                alert_type='power_restored',
                timestamp=now_ts,
                resolved=True,
            )
            return Response(
                {
                    'id': restored.id,
                    'refreshed': False,
                    'deduped': False,
                    'resolved_outage_alerts': resolved_count,
                    'email': None,
                },
                status=status.HTTP_201_CREATED,
            )
        
        # Check if an unresolved alert of this type already exists for this device
        existing_alert = Alert.objects.filter(
            device=device,
            alert_type=alert_type,
            resolved=False
        ).order_by('-last_updated', '-id').first()
        
        if existing_alert:
            # Refresh the existing alert: bump count and move timestamp to latest occurrence.
            existing_alert.timestamp = timezone.now()
            existing_alert.refresh_count += 1
            existing_alert.save(update_fields=['timestamp', 'refresh_count', 'last_updated'])
            alert = existing_alert
            # Unresolved alert refreshes should not repeatedly notify.
            email_result = None
            return Response({'id': alert.id, 'refreshed': True, 'email': email_result}, status=status.HTTP_200_OK)
        else:
            # Create a new alert if none exists
            alert = Alert.objects.create(device=device, alert_type=alert_type, timestamp=timezone.now())
            email_result = _send_alert_notification(alert, is_refresh=False)
            return Response({'id': alert.id, 'refreshed': False, 'email': email_result}, status=status.HTTP_201_CREATED)


class SensorStateView(APIView):
    """Device reports current sensor readings (feeder %, water %)."""

    def get(self, request, device_id):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        device = get_object_or_404(Device, device_id=device_id)
        sensor_state = DeviceSensorState.objects.filter(device=device).first()
        if sensor_state is None:
            sensor_state = DeviceSensorState(device=device)
        serializer = DeviceSensorStateSerializer(sensor_state)
        response_data = serializer.data
        response_data['status'] = 'ok'
        return Response(response_data, status=status.HTTP_200_OK)

    def post(self, request, device_id):
        device = authorize_device(request, device_id)
        if device is None:
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)

        feeder_level = request.data.get('feeder_level_pct')
        water_level = request.data.get('water_level_pct')
        timestamp = request.data.get('timestamp')

        if feeder_level is None or water_level is None:
            return Response(
                {'detail': 'Missing feeder_level_pct or water_level_pct'},
                status=status.HTTP_400_BAD_REQUEST
            )

        try:
            feeder_level = float(feeder_level)
            water_level = float(water_level)
        except (TypeError, ValueError):
            return Response(
                {'detail': 'feeder_level_pct and water_level_pct must be numeric'},
                status=status.HTTP_400_BAD_REQUEST
            )

        # Clamp to 0-100%
        feeder_level = max(0.0, min(100.0, feeder_level))
        water_level = max(0.0, min(100.0, water_level))

        # Parse optional timestamp
        reported_at = None
        if timestamp:
            reported_at = _parse_optional_dt(timestamp)

        received_at = timezone.now()

        # Optional battery fields
        battery_voltage = request.data.get('battery_voltage_v')
        feed_sufficient = _coerce_optional_bool(request.data.get('feed_sufficient'))
        feed_current_kg = request.data.get('feed_current_kg')
        feed_required_next_kg = request.data.get('feed_required_next_kg')

        if feed_current_kg is not None:
            try:
                feed_current_kg = float(feed_current_kg)
            except (TypeError, ValueError):
                return Response({'detail': 'feed_current_kg must be numeric'}, status=status.HTTP_400_BAD_REQUEST)
        if feed_required_next_kg is not None:
            try:
                feed_required_next_kg = float(feed_required_next_kg)
            except (TypeError, ValueError):
                return Response({'detail': 'feed_required_next_kg must be numeric'}, status=status.HTTP_400_BAD_REQUEST)

        event_id = request.data.get('event_id') or request.data.get('request_id')
        boot_id = str(request.data.get('boot_id') or '').strip()
        sequence = _coerce_event_sequence(request.data.get('sequence'))
        source = str(request.data.get('source') or 'esp32').strip() or 'esp32'

        _mark_device_connection_seen(device, received_at, trigger='sensor_state', source=source, extra_payload={'log_type': 'sensor_state'})

        event, sensor_state, duplicate = _ingest_sensor_projection(
            device=device,
            feeder_level=feeder_level,
            water_level=water_level,
            timestamp=reported_at or received_at,
            battery_voltage=battery_voltage,
            feed_sufficient=feed_sufficient,
            feed_current_kg=feed_current_kg,
            feed_required_next_kg=feed_required_next_kg,
            event_id=event_id,
            boot_id=boot_id,
            sequence=sequence,
            source=source,
        )

        if duplicate:
            serializer = DeviceSensorStateSerializer(sensor_state)
            response_data = serializer.data
            response_data['status'] = 'duplicate'
            response_data['event_id'] = event.event_id
            return Response(response_data, status=status.HTTP_200_OK)

        # Auto-trigger and auto-resolve level alerts with hysteresis.
        alert_thresholds = _get_effective_alert_thresholds()
        feeder_low_threshold = float(alert_thresholds.get('alert_feeder_low_threshold_pct', 20.0))
        feeder_recovery_threshold = float(alert_thresholds.get('alert_feeder_high_threshold_pct', 80.0))
        water_low_threshold = float(alert_thresholds.get('alert_water_low_threshold_pct', 20.0))
        water_recovery_threshold = float(alert_thresholds.get('alert_water_high_threshold_pct', 80.0))

        triggered_low_feed_alert = False
        refreshed_low_feed_alert = False
        triggered_low_water_alert = False
        refreshed_low_water_alert = False
        resolved_low_feed_alerts = 0
        resolved_low_water_alerts = 0

        if feeder_level <= feeder_low_threshold:
            existing_low_feed = Alert.objects.filter(
                device=device,
                alert_type='low_feed',
                resolved=False,
            ).order_by('-last_updated', '-id').first()
            if existing_low_feed:
                existing_low_feed.timestamp = timezone.now()
                existing_low_feed.refresh_count += 1
                existing_low_feed.save(update_fields=['timestamp', 'refresh_count', 'last_updated'])
                refreshed_low_feed_alert = True
            else:
                new_low_feed_alert = Alert.objects.create(device=device, alert_type='low_feed', timestamp=timezone.now())
                _send_alert_notification(new_low_feed_alert, is_refresh=False)
                triggered_low_feed_alert = True

        if feeder_level >= feeder_recovery_threshold:
            resolved_low_feed_alerts = Alert.objects.filter(
                device=device,
                alert_type='low_feed',
                resolved=False,
            ).update(resolved=True, last_updated=timezone.now())

        if water_level <= water_low_threshold:
            existing_low_water = Alert.objects.filter(
                device=device,
                alert_type='low_water',
                resolved=False,
            ).order_by('-last_updated', '-id').first()
            if existing_low_water:
                existing_low_water.timestamp = timezone.now()
                existing_low_water.refresh_count += 1
                existing_low_water.save(update_fields=['timestamp', 'refresh_count', 'last_updated'])
                refreshed_low_water_alert = True
            else:
                new_low_water_alert = Alert.objects.create(device=device, alert_type='low_water', timestamp=timezone.now())
                _send_alert_notification(new_low_water_alert, is_refresh=False)
                triggered_low_water_alert = True

        if water_level >= water_recovery_threshold:
            resolved_low_water_alerts = Alert.objects.filter(
                device=device,
                alert_type='low_water',
                resolved=False,
            ).update(resolved=True, last_updated=timezone.now())

        serializer = DeviceSensorStateSerializer(sensor_state)
        response_data = serializer.data
        response_data['status'] = 'updated'
        response_data['triggered_low_feed_alert'] = triggered_low_feed_alert
        response_data['refreshed_low_feed_alert'] = refreshed_low_feed_alert
        response_data['low_feed_alerts_auto_resolved'] = resolved_low_feed_alerts
        response_data['triggered_low_water_alert'] = triggered_low_water_alert
        response_data['refreshed_low_water_alert'] = refreshed_low_water_alert
        response_data['low_water_alerts_auto_resolved'] = resolved_low_water_alerts
        response_data['alert_feeder_low_threshold_pct'] = feeder_low_threshold
        response_data['alert_feeder_recovery_threshold_pct'] = feeder_recovery_threshold
        response_data['alert_water_low_threshold_pct'] = water_low_threshold
        response_data['alert_water_recovery_threshold_pct'] = water_recovery_threshold
        response_data['event_id'] = event.event_id
        return Response(response_data, status=status.HTTP_200_OK)


class FeedNowCommandView(APIView):
    """Create/list feed-now commands for a device."""

    permission_classes = [IsAuthenticated]

    def get(self, request, device_id):
        device = get_object_or_404(Device, device_id=device_id)
        commands = FeedNowCommand.objects.filter(device=device).order_by('-created_at', '-id')[:20]
        serialized = []
        for command in commands:
            command, _ = _reconcile_feed_now_command_from_logs(command)
            serialized.append(_serialize_feed_now_command(command))
        return Response(serialized)

    def post(self, request, device_id):
        device = get_object_or_404(Device, device_id=device_id)
        serializer = FeedNowCommandSerializer(data=request.data)
        if not serializer.is_valid():
            return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)

        amount_kg = float(serializer.validated_data['amount_kg'])
        # Re-read latest device config and determine allowable max for a single feed
        cfg = DeviceConfig.objects.filter(device=device).first()
        allowed_max = _compute_max_single_feed_kg(device, cfg=cfg)
        try:
            allowed_max = float(allowed_max)
        except (TypeError, ValueError):
            allowed_max = float(SystemSettings.get_solo().max_feeds_capacity_kg)

        if amount_kg > allowed_max:
            return Response(
                {'detail': f'amount_kg cannot exceed device max single-feed limit ({allowed_max} kg).'},
                status=status.HTTP_400_BAD_REQUEST,
            )

        pending_commands = FeedNowCommand.objects.filter(
            device=device,
            status=FeedNowCommand.STATUS_PENDING,
        ).order_by('created_at', 'id')
        pending_count = 0
        for pending_command in pending_commands:
            pending_command, _ = _reconcile_feed_now_command_from_logs(pending_command)
            if pending_command.status == FeedNowCommand.STATUS_PENDING:
                pending_count += 1
        if pending_count >= 5:
            return Response(
                {'detail': 'Too many pending feed-now commands. Wait for device acknowledgement.'},
                status=status.HTTP_409_CONFLICT,
            )

        username = request.user.username if request.user and request.user.is_authenticated else ''
        command = FeedNowCommand.objects.create(
            device=device,
            amount_kg=amount_kg,
            requested_by=username,
        )

        return Response(FeedNowCommandSerializer(command).data, status=status.HTTP_201_CREATED)


class FeedNowAcknowledgeView(APIView):
    """Device acknowledgement endpoint for feed-now commands."""

    def post(self, request, device_id, command_id):
        device = authorize_device(request, device_id)
        if device is None:
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        status_value = str(request.data.get('status') or '').strip().lower()
        if status_value not in [FeedNowCommand.STATUS_EXECUTED, FeedNowCommand.STATUS_FAILED]:
            return Response({'detail': 'status must be executed or failed'}, status=status.HTTP_400_BAD_REQUEST)
        event_id = request.data.get('event_id') or request.data.get('request_id')
        boot_id = str(request.data.get('boot_id') or '').strip()
        sequence = _coerce_event_sequence(request.data.get('sequence'))
        source = str(request.data.get('source') or 'esp32').strip() or 'esp32'
        reason = str(request.data.get('reason') or '').strip() or None

        _mark_device_connection_seen(
            device,
            timezone.now(),
            trigger='feed_now_ack',
            source=source,
            extra_payload={'command_id': command_id, 'status': status_value},
        )

        event, command, duplicate, updated = _ack_feed_now_command(
            device=device,
            command_id=command_id,
            status_value=status_value,
            reason=reason,
            event_id=event_id,
            boot_id=boot_id,
            sequence=sequence,
            source=source,
        )

        response_data = FeedNowCommandSerializer(command).data
        response_data['event_id'] = event.event_id
        response_data['duplicate'] = duplicate
        response_data['updated'] = updated
        return Response(response_data, status=status.HTTP_200_OK)


class DeviceEventIngestView(APIView):
    def get(self, request, device_id):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        device = get_object_or_404(Device, device_id=device_id)
        events = DeviceEvent.objects.filter(device=device).order_by('-occurred_at', '-received_at', '-id')

        since = request.query_params.get('since')
        if since:
            parsed_since = _parse_optional_dt(since)
            if parsed_since is None:
                return Response({'detail': 'Invalid since timestamp'}, status=status.HTTP_400_BAD_REQUEST)
            events = events.filter(occurred_at__gte=parsed_since)

        events = events[:200]

        serializer = DeviceEventSerializer(events, many=True)
        return Response(serializer.data)

    def post(self, request, device_id):
        device = authorize_device(request, device_id)
        if device is None:
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)

        raw_events = request.data
        if isinstance(raw_events, dict):
            raw_events = raw_events.get('events', [raw_events])

        if not isinstance(raw_events, list):
            return Response({'detail': 'events must be a list'}, status=status.HTTP_400_BAD_REQUEST)

        if raw_events:
            _mark_device_connection_seen(
                device,
                timezone.now(),
                trigger='batch_events',
                source='esp32',
                extra_payload={'batch_size': len(raw_events)},
            )

        results = []
        for index, item in enumerate(raw_events):
            if not isinstance(item, dict):
                results.append({'index': index, 'accepted': False, 'detail': 'Event must be an object'})
                continue

            event_type = str(item.get('event_type') or item.get('type') or item.get('log_type') or 'generic').strip().lower() or 'generic'
            payload = item.get('payload') if isinstance(item.get('payload'), dict) else {}
            occurred_at = _parse_optional_dt(item.get('occurred_at') or item.get('timestamp')) or timezone.now()
            event_id = item.get('event_id') or item.get('request_id') or payload.get('event_id') or payload.get('request_id')
            boot_id = str(item.get('boot_id') or payload.get('boot_id') or '').strip()
            sequence = _coerce_event_sequence(item.get('sequence', payload.get('sequence')))
            source = str(item.get('source') or payload.get('source') or 'esp32').strip() or 'esp32'

            if event_type in ('sensor_state', 'sensor'):
                feeder_level = item.get('feeder_level_pct', payload.get('feeder_level_pct'))
                water_level = item.get('water_level_pct', payload.get('water_level_pct'))
                if feeder_level is None or water_level is None:
                    results.append({'index': index, 'accepted': False, 'detail': 'Missing feeder_level_pct or water_level_pct'})
                    continue
                try:
                    feeder_level = float(feeder_level)
                    water_level = float(water_level)
                except (TypeError, ValueError):
                    results.append({'index': index, 'accepted': False, 'detail': 'Sensor values must be numeric'})
                    continue
                event, sensor_state, duplicate = _ingest_sensor_projection(
                    device=device,
                    feeder_level=max(0.0, min(100.0, feeder_level)),
                    water_level=max(0.0, min(100.0, water_level)),
                    timestamp=occurred_at,
                    battery_voltage=item.get('battery_voltage_v', payload.get('battery_voltage_v')),
                    feed_sufficient=_coerce_optional_bool(item.get('feed_sufficient', payload.get('feed_sufficient'))),
                    feed_current_kg=item.get('feed_current_kg', payload.get('feed_current_kg')),
                    feed_required_next_kg=item.get('feed_required_next_kg', payload.get('feed_required_next_kg')),
                    event_id=event_id,
                    boot_id=boot_id,
                    sequence=sequence,
                    source=source,
                )
                results.append({
                    'index': index,
                    'accepted': True,
                    'duplicate': duplicate,
                    'event_id': event.event_id,
                    'event_type': event.event_type,
                    'projection': 'sensor_state',
                })
                continue

            if event_type in ('feed_now_ack', 'feed_now_acknowledgement', 'feed_now_command_ack', 'ack'):
                command_id = item.get('command_id', payload.get('command_id'))
                status_value = str(item.get('status') or payload.get('status') or '').strip().lower()
                reason = str(item.get('reason') or payload.get('reason') or '').strip() or None
                if command_id is None:
                    results.append({'index': index, 'accepted': False, 'detail': 'Missing command_id'})
                    continue
                if status_value not in [FeedNowCommand.STATUS_EXECUTED, FeedNowCommand.STATUS_FAILED]:
                    results.append({'index': index, 'accepted': False, 'detail': 'status must be executed or failed'})
                    continue
                try:
                    command_id = int(command_id)
                except (TypeError, ValueError):
                    results.append({'index': index, 'accepted': False, 'detail': 'command_id must be numeric'})
                    continue

                event, command, duplicate, updated = _ack_feed_now_command(
                    device=device,
                    command_id=command_id,
                    status_value=status_value,
                    reason=reason,
                    event_id=event_id,
                    boot_id=boot_id,
                    sequence=sequence,
                    source=source,
                )
                results.append({
                    'index': index,
                    'accepted': True,
                    'duplicate': duplicate,
                    'updated': updated,
                    'event_id': event.event_id,
                    'event_type': event.event_type,
                    'projection': 'feed_now_command',
                    'command_id': command.id,
                    'status': command.status,
                })
                continue

            log_type = event_type
            event, log, duplicate, refreshed = _ingest_log_projection(
                device=device,
                log_type=log_type,
                payload=payload,
                log_timestamp=occurred_at,
                event_id=event_id,
                boot_id=boot_id,
                sequence=sequence,
                source=source,
            )
            results.append({
                'index': index,
                'accepted': True,
                'duplicate': duplicate,
                'refreshed': refreshed,
                'event_id': event.event_id,
                'event_type': event.event_type,
                'projection': 'log',
                'log_id': getattr(log, 'id', None),
                'log_type': getattr(log, 'log_type', log_type),
            })

        return Response({'device_id': device.device_id, 'results': results}, status=status.HTTP_200_OK)
