from django.db.models.signals import post_delete, post_save
from django.dispatch import receiver
from django.utils import timezone

from .models import DeviceConfig, Schedule, SystemSettings


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
    settings_obj = SystemSettings.get_solo()
    return {
        'max_feeds_capacity_kg': settings_obj.max_feeds_capacity_kg,
        'max_feeds_capacity_updated_at': settings_obj.max_feeds_capacity_updated_at.isoformat(),
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


@receiver(post_save, sender=Schedule)
@receiver(post_delete, sender=Schedule)
def refresh_device_config_after_schedule_change(sender, instance, **kwargs):
    device = instance.device
    cfg, _ = DeviceConfig.objects.get_or_create(device=device)

    cfg.config = cfg.config or {}
    cfg.config['schedules'] = _serialize_device_schedules(device)
    cfg.config['system_timezone'] = SystemSettings.get_timezone_name()
    cfg.config.update(_get_effective_max_capacity())
    cfg.config.update(_get_effective_thresholds())
    cfg.updated_by = 'server'
    cfg.last_updated = timezone.now()
    cfg.save(update_fields=['config', 'updated_by', 'last_updated'])
