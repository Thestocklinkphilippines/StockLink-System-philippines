from datetime import timedelta

from django.core.management.base import BaseCommand
from django.utils import timezone

from iot.models import Device
from iot.views import DEVICE_CONNECTION_TIMEOUT_SECONDS, _mark_device_connection_lost


class Command(BaseCommand):
    help = 'Mark devices disconnected when their heartbeat is stale and trigger alerts/logs.'

    def add_arguments(self, parser):
        parser.add_argument(
            '--timeout-seconds',
            type=int,
            default=DEVICE_CONNECTION_TIMEOUT_SECONDS,
            help='Heartbeat timeout window in seconds before a device is considered disconnected.',
        )
        parser.add_argument(
            '--dry-run',
            action='store_true',
            help='Report stale devices without changing state.',
        )

    def handle(self, *args, **options):
        timeout_seconds = max(1, int(options['timeout_seconds']))
        dry_run = bool(options['dry_run'])
        verbosity = int(options.get('verbosity', 1))
        now_ts = timezone.now()
        cutoff = now_ts - timedelta(seconds=timeout_seconds)

        disconnected_count = 0
        skipped_count = 0

        for device in Device.objects.all().order_by('device_id'):
            last_seen = device.last_seen
            status = str(device.connection_status or 'unknown').strip().lower()

            if last_seen is None:
                skipped_count += 1
                continue

            if last_seen >= cutoff:
                skipped_count += 1
                continue

            if status == 'disconnected':
                skipped_count += 1
                continue

            disconnected_count += 1
            if dry_run:
                if verbosity > 0:
                    self.stdout.write(f'[dry-run] {device.device_id} stale since {last_seen.isoformat()}')
                continue

            _mark_device_connection_lost(device, now_ts, trigger='heartbeat_timeout')
            if verbosity > 0:
                self.stdout.write(f'{device.device_id} marked disconnected')

        if verbosity > 0:
            self.stdout.write(
                self.style.SUCCESS(
                    f'Checked {Device.objects.count()} devices, disconnected {disconnected_count}, skipped {skipped_count}.'
                )
            )
