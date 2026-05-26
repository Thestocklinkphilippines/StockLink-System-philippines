from django.core.management.base import BaseCommand

from iot.models import DeviceConfig, SystemSettings, normalize_grain_config


class Command(BaseCommand):
    help = "Populate and normalize DeviceConfig grain config mirrors"

    def handle(self, *args, **options):
        ss = SystemSettings.get_effective()
        gt = ss.get_grain_types()
        qs = DeviceConfig.objects.all()
        total = qs.count()
        updated = 0
        for d in qs:
            cfg = d.config or {}
            cfg.pop('grain_type_index', None)
            normalized = normalize_grain_config(cfg, grain_types=cfg.get('grain_types') or gt)
            if normalized != cfg:
                d.config = normalized
                d.save()
                updated += 1
        self.stdout.write(f"total={total} updated={updated}\n")
