import logging
import os
import sys
import threading
import time

from django.core.management import call_command

logger = logging.getLogger(__name__)

_monitor_started = False
_monitor_lock = threading.Lock()


def _should_start_monitor():
    if 'test' in sys.argv:
        return False
    if os.environ.get('ENABLE_DEVICE_CONNECTION_MONITOR', 'true').strip().lower() not in {'1', 'true', 'yes', 'on'}:
        return False
    return True


def _monitor_loop(interval_seconds):
    while True:
        try:
            call_command('check_device_connections', verbosity=0)
        except Exception:
            logger.exception('Device connection monitor failed')
        time.sleep(interval_seconds)


def start_device_connection_monitor(interval_seconds=15):
    global _monitor_started

    if not _should_start_monitor():
        return False

    with _monitor_lock:
        if _monitor_started:
            return True
        thread = threading.Thread(
            target=_monitor_loop,
            args=(max(5, int(interval_seconds)),),
            name='device-connection-monitor',
            daemon=True,
        )
        thread.start()
        _monitor_started = True
        logger.info('Started device connection monitor thread')
        return True