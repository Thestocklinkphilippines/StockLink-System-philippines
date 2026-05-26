from django.db.models.signals import post_delete, post_save
from django.dispatch import receiver
from django.utils import timezone

from .models import DeviceConfig, Schedule, SystemSettings, default_grain_types, normalize_grain_config


def _serialize_device_schedules(device):
    schedules = []
    for s in Schedule.objects.filter(device=device).order_by('id'):
        schedules.append(
            {
                'id': s.id,
                'schedule_name': s.schedule_name,
                'enabled': s.enabled,
                'days': s.days,
                'time': s.time,
                'feeding_amount_kg': s.feeding_amount_kg,
                'last_updated': s.last_updated.isoformat(),
            }
        )
    return schedules


def _get_effective_max_capacity():
    settings_obj = SystemSettings.get_effective()
    return {
        'max_feeds_capacity_kg': settings_obj.max_feeds_capacity_kg,
        'max_feeds_capacity_updated_at': settings_obj.max_feeds_capacity_updated_at.isoformat(),
        'max_feeds_capacity_updated_by': settings_obj.max_feeds_capacity_updated_by,
    }


def _get_effective_grain_types():
    settings_obj = SystemSettings.get_effective()
    return {
        'grain_types': settings_obj.get_grain_types() if hasattr(settings_obj, 'get_grain_types') else default_grain_types(),
    }


def _resolve_grain_feed_ms_per_kg_from_config(config):
    return normalize_grain_config(config).get('feed_ms_per_kg')


def _apply_effective_grain_config(config, grain_types=None, grain_type=None, grain_type_index=None):
    if not isinstance(config, dict):
        return

    effective_grain_types = grain_types if grain_types is not None else _get_effective_grain_types()['grain_types']
    normalized = normalize_grain_config(
        config,
        grain_types=effective_grain_types,
        grain_type=grain_type if grain_type is not None else config.get('grain_type'),
        grain_type_index=grain_type_index if grain_type_index is not None else config.get('grain_type_index'),
    )
    config.clear()
    config.update(normalized)


def _get_effective_grain_profile():
    settings_obj = SystemSettings.get_effective()
    grain_types = settings_obj.get_grain_types() if hasattr(settings_obj, 'get_grain_types') else default_grain_types()
    selected = normalize_grain_config({'grain_type': settings_obj.grain_type}, grain_types=grain_types, grain_type=settings_obj.grain_type)
    return {
        'grain_type': selected.get('grain_type'),
        'grain_type_index': selected.get('grain_type_index'),
        'feed_ms_per_kg': selected.get('feed_ms_per_kg'),
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


@receiver(post_save, sender=Schedule)
@receiver(post_delete, sender=Schedule)
def refresh_device_config_after_schedule_change(sender, instance, **kwargs):
    device = instance.device
    cfg, _ = DeviceConfig.objects.get_or_create(device=device)

    cfg.config = cfg.config or {}
    cfg.config['schedules'] = _serialize_device_schedules(device)
    cfg.config['system_timezone'] = SystemSettings.get_timezone_name()
    cfg.config.update(_get_effective_max_capacity())
    _apply_effective_grain_config(
        cfg.config,
        grain_types=_get_effective_grain_types()['grain_types'],
        grain_type=SystemSettings.get_effective().grain_type,
    )
    cfg.config.update(_get_effective_thresholds())
    cfg.config.update(_get_effective_alert_thresholds())
    cfg.config.setdefault('low_battery_shutdown_v', _get_effective_battery_shutdown_threshold()['low_battery_shutdown_v'])
    cfg.updated_by = 'server'
    cfg.last_updated = timezone.now()
    cfg.save(update_fields=['config', 'updated_by', 'last_updated'])


@receiver(post_save, sender=SystemSettings)
def refresh_device_configs_after_system_settings_change(sender, instance, created, **kwargs):
    updated_fields = set(getattr(instance, '_changed_system_setting_fields', set()) or set())
    signal_update_fields = kwargs.get('update_fields')
    if signal_update_fields:
        updated_fields.update(signal_update_fields)

    full_save_without_field_hints = not updated_fields and not created

    should_update_low_battery = full_save_without_field_hints or 'low_battery_shutdown_v' in updated_fields
    should_update_grain_types = full_save_without_field_hints or 'grain_types' in updated_fields
    should_update_grain_type = full_save_without_field_hints or 'grain_type' in updated_fields

    for cfg in DeviceConfig.objects.all():
        config = cfg.config or {}
        changed = False

        if should_update_low_battery:
            config['low_battery_shutdown_v'] = instance.low_battery_shutdown_v
            changed = True

        if should_update_grain_types:
            config['grain_types'] = instance.get_grain_types() if hasattr(instance, 'get_grain_types') else default_grain_types()
            changed = True

        if should_update_grain_type:
            config['grain_type'] = instance.grain_type
            changed = True

        if should_update_grain_types or should_update_grain_type:
            config.pop('grain_type_index', None)
            config.update(
                normalize_grain_config(
                    config,
                    grain_types=config.get('grain_types') or (instance.get_grain_types() if hasattr(instance, 'get_grain_types') else default_grain_types()),
                    grain_type=instance.grain_type,
                )
            )
            changed = True

        if not changed:
            continue

        cfg.config = config
        cfg.updated_by = 'server'
        cfg.last_updated = timezone.now()
        cfg.save(update_fields=['config', 'updated_by', 'last_updated'])
