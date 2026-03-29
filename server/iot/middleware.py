from django.conf import settings
from django.db.utils import OperationalError, ProgrammingError
from django.utils import timezone


def _safe_timezone_name():
    try:
        from .models import SystemSettings
        return SystemSettings.get_timezone_name()
    except (OperationalError, ProgrammingError):
        return settings.TIME_ZONE
    except Exception:
        return settings.TIME_ZONE


class SystemTimezoneMiddleware:
    """Activate DB-configured timezone per request for server-side time operations."""

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        timezone.activate(_safe_timezone_name())
        response = self.get_response(request)
        timezone.deactivate()
        return response
