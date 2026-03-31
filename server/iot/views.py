from django.utils import timezone
from datetime import datetime
from django.shortcuts import get_object_or_404
from django.utils.dateparse import parse_datetime
from zoneinfo import available_timezones
from rest_framework.views import APIView
from rest_framework.response import Response
from rest_framework import status
from rest_framework.permissions import IsAdminUser

from .models import Device, DeviceConfig, Alert, Log, SystemSettings, DeviceSensorState
from .serializers import DeviceSerializer, DeviceConfigSerializer, AlertSerializer, LogSerializer, SystemSettingsSerializer, DeviceSensorStateSerializer
import secrets

from rest_framework.permissions import IsAdminUser

from .serializers import ScheduleSerializer
from .models import Schedule
from django.forms.models import model_to_dict


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
    settings_obj = SystemSettings.get_solo()
    return {
        'max_feeds_capacity_kg': settings_obj.max_feeds_capacity_kg,
        'max_feeds_capacity_updated_at': _normalize_dt(settings_obj.max_feeds_capacity_updated_at).isoformat(),
        'max_feeds_capacity_updated_by': settings_obj.max_feeds_capacity_updated_by,
    }


def _get_effective_thresholds():
    settings_obj = SystemSettings.get_solo()
    return {
        'feeder_low_threshold_pct': settings_obj.feeder_low_threshold_pct,
        'feeder_high_threshold_pct': settings_obj.feeder_high_threshold_pct,
        'water_low_threshold_pct': settings_obj.water_low_threshold_pct,
        'water_high_threshold_pct': settings_obj.water_high_threshold_pct,
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
        settings_obj.save()
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
    cfg.updated_by = 'server'
    cfg.last_updated = timezone.now()
    cfg.save()


def _get_device_sensor_state(device):
    """Get current sensor readings for a device (or defaults if not set)."""
    try:
        sensor = DeviceSensorState.objects.get(device=device)
        return {
            'feeder_level_pct': sensor.feeder_level_pct,
            'water_level_pct': sensor.water_level_pct,
            'last_reported_at': sensor.last_reported_at.isoformat() if sensor.last_reported_at else None,
        }
    except DeviceSensorState.DoesNotExist:
        return {
            'feeder_level_pct': 100.0,
            'water_level_pct': 100.0,
            'last_reported_at': None,
        }


def _get_effective_config_last_updated(device, cfg):
    latest_schedule = (
        Schedule.objects.filter(device=device)
        .order_by('-last_updated')
        .values_list('last_updated', flat=True)
        .first()
    )
    latest_settings = SystemSettings.get_solo().updated_at

    effective = cfg.last_updated
    if latest_schedule and latest_schedule > effective:
        effective = latest_schedule
    if latest_settings and latest_settings > effective:
        effective = latest_settings
    return effective


def _with_runtime_time_fields(payload):
    now_utc = timezone.now()
    now_local = timezone.localtime(now_utc)
    data = dict(payload)
    data.update(_get_effective_max_capacity())
    data.update(_get_effective_thresholds())
    data['server_time_utc'] = now_utc.isoformat()
    data['server_time_local'] = now_local.isoformat()
    data['timezone_effective'] = _get_system_timezone_name()
    return data


class ScheduleListCreateView(APIView):
    """List/create schedules for a device. Authenticated web user allowed; devices should not use this endpoint."""

    def get(self, request, device_id):
        device = get_object_or_404(Device, device_id=device_id)
        qs = Schedule.objects.filter(device=device).order_by('id')
        serializer = ScheduleSerializer(qs, many=True)
        return Response(serializer.data)

    def post(self, request, device_id):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
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
    def get(self, request, device_id, pk):
        device = get_object_or_404(Device, device_id=device_id)
        sched = get_object_or_404(Schedule, pk=pk, device=device)
        return Response(ScheduleSerializer(sched).data)

    def put(self, request, device_id, pk):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
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
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
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
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
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

    def get(self, request):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        devices = Device.objects.order_by('device_id').values_list('device_id', flat=True)
        return Response({'devices': list(devices)})


class SystemSettingsView(APIView):
    """Get/update global system settings shared by frontend and devices."""

    def get(self, request):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        settings_obj = SystemSettings.get_solo()
        serialized = SystemSettingsSerializer(settings_obj).data
        payload = _with_runtime_time_fields(serialized)
        payload['timezone_options'] = sorted(available_timezones())
        return Response(payload)

    def patch(self, request):
        if not (request.user and request.user.is_authenticated):
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        settings_obj = SystemSettings.get_solo()
        serializer = SystemSettingsSerializer(settings_obj, data=request.data, partial=True)
        if serializer.is_valid():
            serializer.save()
            if 'max_feeds_capacity_kg' in serializer.validated_data:
                _maybe_update_max_capacity(serializer.validated_data['max_feeds_capacity_kg'], 'server', timezone.now())
                serializer = SystemSettingsSerializer(SystemSettings.get_solo())
            payload = _with_runtime_time_fields(serializer.data)
            payload['timezone_options'] = sorted(available_timezones())
            return Response(payload)
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)


class RegisterUserView(APIView):
    """Public endpoint to create the single frontend user account."""

    def post(self, request):
        username = request.data.get('username')
        password = request.data.get('password')
        if not username or not password:
            return Response({'detail': 'username and password required'}, status=status.HTTP_400_BAD_REQUEST)
        from django.contrib.auth.models import User
        if User.objects.filter(username=username).exists():
            return Response({'detail': 'username already exists'}, status=status.HTTP_400_BAD_REQUEST)
        user = User.objects.create_user(username=username, password=password)
        return Response({'username': user.username}, status=status.HTTP_201_CREATED)


class LoginUserView(APIView):
    def post(self, request):
        from django.contrib.auth import authenticate, login
        username = request.data.get('username')
        password = request.data.get('password')
        if not username or not password:
            return Response({'detail': 'username and password required'}, status=status.HTTP_400_BAD_REQUEST)
        user = authenticate(request, username=username, password=password)
        if user is None:
            return Response({'detail': 'invalid credentials'}, status=status.HTTP_401_UNAUTHORIZED)
        login(request, user)
        return Response({'username': user.username})


class LogoutUserView(APIView):
    def post(self, request):
        from django.contrib.auth import logout
        logout(request)
        return Response({'detail': 'logged out'})


class CurrentUserView(APIView):
    """Return brief info about the currently authenticated web user."""

    def get(self, request):
        if request.user and request.user.is_authenticated:
            return Response({'is_authenticated': True, 'username': request.user.username})
        return Response({'is_authenticated': False}, status=status.HTTP_200_OK)


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

    Server (management) pushes must be authenticated as admin (session/basic auth)
    and will overwrite the device config setting `updated_by='server'`.
    """

    def get(self, request, device_id):
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
        data['config'].update(_get_effective_thresholds())
        data['config']['last_updated'] = data['last_updated']
        data['config']['updated_by'] = data.get('updated_by') or 'server'
        # Include current sensor state
        data['sensor_state'] = _get_device_sensor_state(device)
        return Response(_with_runtime_time_fields(data))

    def post(self, request, device_id):
        # Try device auth first
        device = authorize_device(request, device_id)

        # Server (management) push via authenticated web user (allow single frontend user)
        if device is None and request.user and request.user.is_authenticated:
            # Authenticated web user is pushing config to device
            device = get_object_or_404(Device, device_id=device_id)
            payload = request.data.get('config')
            if payload is None:
                return Response({'detail': 'Missing config'}, status=status.HTTP_400_BAD_REQUEST)

            candidate_dt = _parse_optional_dt(payload.get('max_feeds_capacity_updated_at')) or timezone.now()
            _maybe_update_max_capacity(payload.get('max_feeds_capacity_kg'), 'server', candidate_dt)

            cfg, _ = DeviceConfig.objects.get_or_create(device=device)
            cfg.config = payload
            cfg.config['system_timezone'] = _get_system_timezone_name()
            cfg.config.update(_get_effective_max_capacity())
            cfg.config.update(_get_effective_thresholds())
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

        cfg.config = payload
        cfg.config['system_timezone'] = _get_system_timezone_name()
        cfg.config.update(_get_effective_max_capacity())
        cfg.config.update(_get_effective_thresholds())
        cfg.updated_by = 'esp32'
        cfg.last_updated = inc_dt if inc_dt.tzinfo else timezone.make_aware(inc_dt)
        cfg.save()
        return Response(_with_runtime_time_fields(DeviceConfigSerializer(cfg).data))

class LogsView(APIView):
    def get(self, request, device_id):
        # Return recent logs for the device (paginated simple view)
        device = get_object_or_404(Device, device_id=device_id)
        logs = Log.objects.filter(device=device).order_by('-timestamp')[:200]
        serializer = LogSerializer(logs, many=True)
        return Response(serializer.data)

    def post(self, request, device_id):
        device = authorize_device(request, device_id)
        if device is None:
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        log_type = request.data.get('log_type', 'generic')
        payload = request.data.get('payload', {})
        timestamp_str = request.data.get('timestamp')
        
        # Parse timestamp from ESP32 (ISO8601 format expected)
        if not timestamp_str:
            return Response({'detail': 'Missing timestamp'}, status=status.HTTP_400_BAD_REQUEST)
        
        log_timestamp = _parse_optional_dt(timestamp_str)
        if log_timestamp is None:
            return Response({'detail': 'Invalid timestamp format'}, status=status.HTTP_400_BAD_REQUEST)
        
        # Check if a log of this type already exists for this device (get most recent)
        existing_log = Log.objects.filter(device=device, log_type=log_type).order_by('-id').first()
        
        if existing_log:
            # Refresh the existing log: update timestamp and payload, increment count
            existing_log.timestamp = log_timestamp
            existing_log.payload = payload
            existing_log.refresh_count += 1
            existing_log.save(update_fields=['timestamp', 'payload', 'refresh_count', 'last_updated'])
            log = existing_log
            return Response({'id': log.id, 'refreshed': True}, status=status.HTTP_200_OK)
        else:
            # Create a new log if none exists
            log = Log.objects.create(device=device, log_type=log_type, payload=payload, timestamp=log_timestamp)
            return Response({'id': log.id, 'refreshed': False}, status=status.HTTP_201_CREATED)

class AlertsView(APIView):
    def get(self, request, device_id):
        device = get_object_or_404(Device, device_id=device_id)
        alerts = Alert.objects.filter(device=device).order_by('-last_updated', '-id')[:200]
        serializer = AlertSerializer(alerts, many=True)
        return Response(serializer.data)

    def post(self, request, device_id):
        device = authorize_device(request, device_id)
        if device is None:
            return Response({'detail': 'Unauthorized'}, status=status.HTTP_401_UNAUTHORIZED)
        alert_type = request.data.get('alert_type')
        if not alert_type:
            return Response({'detail': 'Missing alert_type'}, status=status.HTTP_400_BAD_REQUEST)
        
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
            return Response({'id': alert.id, 'refreshed': True}, status=status.HTTP_200_OK)
        else:
            # Create a new alert if none exists
            alert = Alert.objects.create(device=device, alert_type=alert_type, timestamp=timezone.now())
            return Response({'id': alert.id, 'refreshed': False}, status=status.HTTP_201_CREATED)


class SensorStateView(APIView):
    """Device reports current sensor readings (feeder %, water %)."""

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

        sensor_state, _ = DeviceSensorState.objects.get_or_create(device=device)
        sensor_state.feeder_level_pct = feeder_level
        sensor_state.water_level_pct = water_level
        sensor_state.last_reported_at = reported_at or timezone.now()
        sensor_state.save()

        # Auto-resolve low-feed alerts only after feeder recovers past the high threshold (hysteresis).
        thresholds = _get_effective_thresholds()
        feeder_recovery_threshold = float(thresholds.get('feeder_high_threshold_pct', 80.0))
        resolved_low_feed_alerts = 0
        if feeder_level >= feeder_recovery_threshold:
            resolved_low_feed_alerts = Alert.objects.filter(
                device=device,
                alert_type='low_feed',
                resolved=False,
            ).update(resolved=True, last_updated=timezone.now())

        serializer = DeviceSensorStateSerializer(sensor_state)
        response_data = serializer.data
        response_data['status'] = 'updated'
        response_data['low_feed_alerts_auto_resolved'] = resolved_low_feed_alerts
        response_data['feeder_recovery_threshold_pct'] = feeder_recovery_threshold
        return Response(response_data, status=status.HTTP_200_OK)
