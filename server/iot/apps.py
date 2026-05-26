from django.apps import AppConfig

class IotConfig(AppConfig):
    default_auto_field = 'django.db.models.BigAutoField'
    name = 'iot'

    def ready(self):
        # Register model signal handlers.
        from . import signals  # noqa: F401
        from .connection_monitor import start_device_connection_monitor

        start_device_connection_monitor()
