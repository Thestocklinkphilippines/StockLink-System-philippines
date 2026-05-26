import math

from django.db import models
from django.conf import settings
from django.utils import timezone as django_timezone


def default_grain_types():
    return [
        {'grain_type': 'mash', 'feed_ms_per_kg': 137820.8},
        {'grain_type': 'crumbles', 'feed_ms_per_kg': 7000.0},
        {'grain_type': 'mini_pellets', 'feed_ms_per_kg': 4200.0},
        {'grain_type': 'standard_pellets', 'feed_ms_per_kg': 5200.0},
        {'grain_type': 'large_pellets', 'feed_ms_per_kg': 6100.0},
    ]


def _coerce_grain_types(grain_types):
    if not isinstance(grain_types, list):
        return default_grain_types()

    normalized = []
    for item in grain_types:
        if not isinstance(item, dict):
            continue
        grain_type = str(item.get('grain_type') or '').strip()
        if not grain_type:
            continue
        try:
            feed_ms_per_kg = float(item.get('feed_ms_per_kg'))
        except (TypeError, ValueError):
            feed_ms_per_kg = None
        normalized.append({'grain_type': grain_type, 'feed_ms_per_kg': feed_ms_per_kg})

    return normalized or default_grain_types()


def _coerce_grain_index(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _find_grain_type_index(grain_types, grain_type):
    target = str(grain_type or '').strip()
    if not target:
        return None

    for index, item in enumerate(_coerce_grain_types(grain_types)):
        if str(item.get('grain_type') or '') == target:
            return index
    return None


def _resolve_selected_grain_item(grain_types, grain_type=None, grain_type_index=None):
    normalized_grain_types = _coerce_grain_types(grain_types)
    selected_index = _coerce_grain_index(grain_type_index)
    selected_item = None

    if selected_index is not None and 0 <= selected_index < len(normalized_grain_types):
        selected_item = normalized_grain_types[selected_index]
    else:
        fallback_index = _find_grain_type_index(normalized_grain_types, grain_type)
        if fallback_index is not None:
            selected_index = fallback_index
            selected_item = normalized_grain_types[fallback_index]

    if selected_item is None:
        selected_index = 0 if normalized_grain_types else 0
        selected_item = normalized_grain_types[0] if normalized_grain_types else {
            'grain_type': str(grain_type or '').strip(),
            'feed_ms_per_kg': None,
        }

    selected_name = str(selected_item.get('grain_type') or '').strip()
    if not selected_name:
        selected_name = str(grain_type or '').strip()
    if not selected_name and normalized_grain_types:
        selected_name = str(normalized_grain_types[0].get('grain_type') or '').strip()

    selected_rate = selected_item.get('feed_ms_per_kg')
    try:
        selected_rate = float(selected_rate)
    except (TypeError, ValueError):
        selected_rate = None

    if selected_rate is None and selected_name:
        rate_index = _find_grain_type_index(normalized_grain_types, selected_name)
        if rate_index is not None:
            try:
                selected_rate = float(normalized_grain_types[rate_index].get('feed_ms_per_kg'))
            except (TypeError, ValueError):
                selected_rate = None

    if selected_index is None:
        selected_index = 0

    return normalized_grain_types, selected_index, selected_name, selected_rate


def normalize_grain_config(config, grain_types=None, grain_type=None, grain_type_index=None):
    normalized = dict(config or {}) if isinstance(config, dict) else {}
    normalized_grain_types = _coerce_grain_types(grain_types if grain_types is not None else normalized.get('grain_types'))
    resolved_grain_types, resolved_index, resolved_name, resolved_rate = _resolve_selected_grain_item(
        normalized_grain_types,
        grain_type=grain_type if grain_type is not None else normalized.get('grain_type'),
        grain_type_index=grain_type_index if grain_type_index is not None else normalized.get('grain_type_index'),
    )

    normalized['grain_types'] = resolved_grain_types
    normalized['grain_type_index'] = resolved_index
    normalized['grain_type'] = resolved_name
    normalized['feed_ms_per_kg'] = resolved_rate
    return normalized


def _coerce_optional_float(value):
    if value in (None, ''):
        return None

    try:
        numeric_value = float(value)
    except (TypeError, ValueError):
        return None

    if not math.isfinite(numeric_value):
        return None

    return numeric_value


def normalize_water_tank_config(config):
    normalized = dict(config or {}) if isinstance(config, dict) else {}

    for field_name in ('water_tank_full_cm', 'water_tank_depth_cm'):
        if field_name not in normalized:
            continue

        coerced_value = _coerce_optional_float(normalized.get(field_name))
        if coerced_value is not None:
            normalized[field_name] = coerced_value

    return normalized

class Device(models.Model):
    device_id = models.CharField(max_length=128, unique=True)
    auth_token = models.CharField(max_length=128, blank=True)
    last_seen = models.DateTimeField(null=True, blank=True)
    connection_status = models.CharField(max_length=32, default='unknown')

    def __str__(self):
        return self.device_id

class DeviceConfig(models.Model):
    device = models.OneToOneField(Device, on_delete=models.CASCADE)
    config = models.JSONField(default=dict)
    last_updated = models.DateTimeField(auto_now=True)
    updated_by = models.CharField(max_length=16, default='esp32')

    def save(self, *args, **kwargs):
        # Ensure config is a dict
        if not isinstance(self.config, dict):
            self.config = {}

        # Normalize the grain selection mirrors before persisting.
        try:
            settings_obj = SystemSettings.get_effective()
            self.config = normalize_grain_config(
                self.config,
                grain_types=self.config.get('grain_types') or settings_obj.get_grain_types(),
            )
        except Exception:
            # Don't let unexpected errors prevent saving; leave config unchanged
            pass

        self.config = normalize_water_tank_config(self.config)

        super().save(*args, **kwargs)


class Schedule(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE, related_name='schedules')
    schedule_name = models.CharField(max_length=128)
    enabled = models.BooleanField(default=True)
    days = models.JSONField(default=list)  # list of day strings e.g. ["Mon","Tue"]
    time = models.CharField(max_length=8)  # HH:MM
    feeding_amount_kg = models.FloatField()
    last_updated = models.DateTimeField(auto_now=True)

    def __str__(self):
        return f"{self.schedule_name} ({self.device.device_id})"

class Alert(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    alert_type = models.CharField(max_length=64)
    timestamp = models.DateTimeField()  # When alert first occurred
    last_updated = models.DateTimeField(auto_now=True)  # Last time alert was seen/refreshed
    refresh_count = models.IntegerField(default=1)  # How many times we've seen this alert
    resolved = models.BooleanField(default=False)
    payload = models.JSONField(default=dict)

class Log(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    log_type = models.CharField(max_length=64)
    log_category = models.CharField(max_length=32, default='system')
    payload = models.JSONField(default=dict)
    timestamp = models.DateTimeField()  # Timestamp of event (from ESP32)
    last_updated = models.DateTimeField(auto_now=True)  # When we last saw this log
    refresh_count = models.IntegerField(default=1)  # How many times we've seen this log type


class DeviceEvent(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE, related_name='events')
    event_id = models.CharField(max_length=128)
    event_type = models.CharField(max_length=64)
    occurred_at = models.DateTimeField()
    received_at = models.DateTimeField(auto_now_add=True)
    boot_id = models.CharField(max_length=128, blank=True, default='')
    sequence = models.BigIntegerField(null=True, blank=True)
    source = models.CharField(max_length=32, blank=True, default='esp32')
    payload = models.JSONField(default=dict)
    delivery_status = models.CharField(max_length=32, default='accepted')

    class Meta:
        constraints = [
            models.UniqueConstraint(fields=['device', 'event_id'], name='uniq_device_event_id'),
        ]
        indexes = [
            models.Index(fields=['device', '-occurred_at']),
            models.Index(fields=['device', 'event_type', '-occurred_at']),
        ]

    def __str__(self):
        return f"{self.device.device_id} {self.event_type} {self.event_id}"


class SystemSettings(models.Model):
    timezone = models.CharField(max_length=64, default=settings.TIME_ZONE)
    max_feeds_capacity_kg = models.FloatField(default=1.0)
    grain_type = models.CharField(max_length=32, default='standard_pellets')
    grain_types = models.JSONField(default=default_grain_types)
    feeder_low_threshold_pct = models.FloatField(default=20.0)
    feeder_high_threshold_pct = models.FloatField(default=80.0)
    water_low_threshold_pct = models.FloatField(default=20.0)
    water_high_threshold_pct = models.FloatField(default=80.0)
    alert_feeder_low_threshold_pct = models.FloatField(default=20.0)
    alert_feeder_high_threshold_pct = models.FloatField(default=80.0)
    alert_water_low_threshold_pct = models.FloatField(default=20.0)
    alert_water_high_threshold_pct = models.FloatField(default=80.0)
    low_battery_shutdown_v = models.FloatField(default=10.0)
    important_log_keywords = models.JSONField(default=list)
    alert_recipients = models.JSONField(default=list)
    smtp_email_user = models.CharField(max_length=255, blank=True, default='')
    smtp_email_password = models.CharField(max_length=255, blank=True, default='')
    max_feeds_capacity_updated_at = models.DateTimeField(default=django_timezone.now)
    max_feeds_capacity_updated_by = models.CharField(max_length=16, default='server')
    updated_at = models.DateTimeField(auto_now=True)

    def __str__(self):
        return f"System Settings ({self.timezone})"

    @classmethod
    def get_solo(cls):
        obj, _ = cls.objects.get_or_create(
            pk=1,
            defaults={
                'timezone': settings.TIME_ZONE,
                'max_feeds_capacity_kg': 1.0,
                'grain_type': 'standard_pellets',
                'grain_types': default_grain_types(),
                'feeder_low_threshold_pct': 20.0,
                'feeder_high_threshold_pct': 80.0,
                'water_low_threshold_pct': 20.0,
                'water_high_threshold_pct': 80.0,
                'alert_feeder_low_threshold_pct': 20.0,
                'alert_feeder_high_threshold_pct': 80.0,
                'alert_water_low_threshold_pct': 20.0,
                'alert_water_high_threshold_pct': 80.0,
                'low_battery_shutdown_v': 10.0,
                'important_log_keywords': ['critical', 'error', 'fault', 'warning', 'alert', 'offline', 'fail'],
                'alert_recipients': [],
                'smtp_email_user': '',
                'smtp_email_password': '',
                'max_feeds_capacity_updated_at': django_timezone.now(),
                'max_feeds_capacity_updated_by': 'server',
            },
        )
        return obj

    @classmethod
    def get_effective(cls):
        obj = cls.objects.filter(pk=1).first()
        if obj is not None:
            return obj
        return cls()

    @classmethod
    def get_timezone_name(cls):
        return cls.get_solo().timezone

    def get_grain_types(self):
        return self.grain_types or default_grain_types()

    def resolve_grain_feed_ms_per_kg(self):
        for item in self.get_grain_types():
            if isinstance(item, dict) and str(item.get('grain_type') or '') == self.grain_type:
                try:
                    return float(item.get('feed_ms_per_kg'))
                except (TypeError, ValueError):
                    return None
        return None


class DeviceSensorState(models.Model):
    """Current sensor readings (feeder level %, water level %) from the device."""
    device = models.OneToOneField(Device, on_delete=models.CASCADE, related_name='sensor_state')
    feeder_level_pct = models.FloatField(default=100.0)  # 0-100%
    water_level_pct = models.FloatField(default=100.0)   # 0-100%
    # Battery sensing (optional)
    battery_voltage_v = models.FloatField(null=True, blank=True)
    # Feed sufficiency telemetry published by firmware.
    feed_sufficient = models.BooleanField(null=True, blank=True)
    feed_current_kg = models.FloatField(null=True, blank=True)
    feed_required_next_kg = models.FloatField(null=True, blank=True)
    last_reported_at = models.DateTimeField(null=True, blank=True)  # timestamp device sent
    updated_at = models.DateTimeField(auto_now=True)

    def __str__(self):
        return f"{self.device.device_id}: Feeder {self.feeder_level_pct:.1f}%, Water {self.water_level_pct:.1f}%"


class FeedNowCommand(models.Model):
    STATUS_PENDING = 'pending'
    STATUS_EXECUTED = 'executed'
    STATUS_FAILED = 'failed'
    STATUS_CHOICES = [
        (STATUS_PENDING, 'Pending'),
        (STATUS_EXECUTED, 'Executed'),
        (STATUS_FAILED, 'Failed'),
    ]

    device = models.ForeignKey(Device, on_delete=models.CASCADE, related_name='feed_now_commands')
    amount_kg = models.FloatField()
    status = models.CharField(max_length=16, choices=STATUS_CHOICES, default=STATUS_PENDING)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)
    requested_by = models.CharField(max_length=150, blank=True, default='')
    executed_at = models.DateTimeField(null=True, blank=True)
    failure_reason = models.CharField(max_length=255, blank=True, default='')

    class Meta:
        ordering = ['-created_at', '-id']

    def __str__(self):
        return f"FeedNow {self.device.device_id} {self.amount_kg}kg [{self.status}]"


class UserApproval(models.Model):
    """Tracks admin approval state for a web user account.

    Kept separate from the User model to avoid a custom user model.
    """
    user = models.OneToOneField(settings.AUTH_USER_MODEL, on_delete=models.CASCADE, related_name='approval')
    is_approved = models.BooleanField(default=False)
    approved_by = models.ForeignKey(settings.AUTH_USER_MODEL, null=True, blank=True, on_delete=models.SET_NULL, related_name='+')
    approved_at = models.DateTimeField(null=True, blank=True)
    rejected = models.BooleanField(default=False)
    rejected_reason = models.TextField(blank=True, default='')
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Approval({self.user.username}): {'approved' if self.is_approved else 'pending'}"


class AdminRoleVote(models.Model):
    VOTE_PROMOTE = 'promote'
    VOTE_DEMOTE = 'demote'
    VOTE_CHOICES = [
        (VOTE_PROMOTE, 'Promote'),
        (VOTE_DEMOTE, 'Demote'),
    ]

    target_user = models.ForeignKey(settings.AUTH_USER_MODEL, on_delete=models.CASCADE, related_name='admin_role_votes')
    voter = models.ForeignKey(settings.AUTH_USER_MODEL, on_delete=models.CASCADE, related_name='admin_role_votes_cast')
    vote_type = models.CharField(max_length=16, choices=VOTE_CHOICES)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        constraints = [
            models.UniqueConstraint(fields=['target_user', 'voter', 'vote_type'], name='uniq_admin_role_vote'),
        ]

    def __str__(self):
        return f"{self.voter.username} -> {self.target_user.username} [{self.vote_type}]"
