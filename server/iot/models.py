from django.db import models
from django.conf import settings
from django.utils import timezone as django_timezone

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

class Log(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    log_type = models.CharField(max_length=64)
    payload = models.JSONField(default=dict)
    timestamp = models.DateTimeField()  # Timestamp of event (from ESP32)
    last_updated = models.DateTimeField(auto_now=True)  # When we last saw this log
    refresh_count = models.IntegerField(default=1)  # How many times we've seen this log type


class SystemSettings(models.Model):
    timezone = models.CharField(max_length=64, default=settings.TIME_ZONE)
    max_feeds_capacity_kg = models.FloatField(default=1.0)
    feeder_low_threshold_pct = models.FloatField(default=20.0)
    feeder_high_threshold_pct = models.FloatField(default=80.0)
    water_low_threshold_pct = models.FloatField(default=20.0)
    water_high_threshold_pct = models.FloatField(default=80.0)
    alert_feeder_low_threshold_pct = models.FloatField(default=20.0)
    alert_feeder_high_threshold_pct = models.FloatField(default=80.0)
    alert_water_low_threshold_pct = models.FloatField(default=20.0)
    alert_water_high_threshold_pct = models.FloatField(default=80.0)
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
                'feeder_low_threshold_pct': 20.0,
                'feeder_high_threshold_pct': 80.0,
                'water_low_threshold_pct': 20.0,
                'water_high_threshold_pct': 80.0,
                'alert_feeder_low_threshold_pct': 20.0,
                'alert_feeder_high_threshold_pct': 80.0,
                'alert_water_low_threshold_pct': 20.0,
                'alert_water_high_threshold_pct': 80.0,
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
    def get_timezone_name(cls):
        return cls.get_solo().timezone


class DeviceSensorState(models.Model):
    """Current sensor readings (feeder level %, water level %) from the device."""
    device = models.OneToOneField(Device, on_delete=models.CASCADE, related_name='sensor_state')
    feeder_level_pct = models.FloatField(default=100.0)  # 0-100%
    water_level_pct = models.FloatField(default=100.0)   # 0-100%
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
