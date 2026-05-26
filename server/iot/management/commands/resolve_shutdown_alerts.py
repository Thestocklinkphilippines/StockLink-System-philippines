from django.core.management.base import BaseCommand

from iot.views import LOW_BATTERY_SHUTDOWN_RESOLVE_AFTER_SECONDS, _resolve_stale_shutdown_alerts


class Command(BaseCommand):
    help = 'Resolve stale low-battery shutdown alerts after the configured grace period.'

    def add_arguments(self, parser):
        parser.add_argument(
            '--grace-seconds',
            type=int,
            default=LOW_BATTERY_SHUTDOWN_RESOLVE_AFTER_SECONDS,
            help='How long a shutdown alert may remain unresolved before being auto-resolved.',
        )

    def handle(self, *args, **options):
        grace_seconds = max(1, int(options['grace_seconds']))
        resolved_count = _resolve_stale_shutdown_alerts(grace_seconds=grace_seconds)
        self.stdout.write(self.style.SUCCESS(f'Resolved {resolved_count} stale shutdown alert(s).'))
