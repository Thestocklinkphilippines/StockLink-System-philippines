from rest_framework import serializers
from django.core.exceptions import ValidationError as DjangoValidationError
from django.core.validators import validate_email
from .models import Device, DeviceConfig, Alert, Log, DeviceSensorState, FeedNowCommand
from zoneinfo import ZoneInfo

from .models import Schedule
from .models import SystemSettings

class DeviceSerializer(serializers.ModelSerializer):
    class Meta:
        model = Device
        fields = ['device_id', 'auth_token', 'last_seen', 'connection_status']

class DeviceConfigSerializer(serializers.ModelSerializer):
    class Meta:
        model = DeviceConfig
        fields = ['device', 'config', 'last_updated', 'updated_by']


class ScheduleSerializer(serializers.ModelSerializer):
    class Meta:
        model = Schedule
        fields = ['id', 'device', 'schedule_name', 'enabled', 'days', 'time', 'feeding_amount_kg', 'last_updated']

class AlertSerializer(serializers.ModelSerializer):
    class Meta:
        model = Alert
        fields = ['id', 'device', 'alert_type', 'timestamp', 'last_updated', 'refresh_count', 'resolved']

class LogSerializer(serializers.ModelSerializer):
    class Meta:
        model = Log
        fields = ['id', 'device', 'log_type', 'payload', 'timestamp', 'last_updated', 'refresh_count']


class SystemSettingsSerializer(serializers.ModelSerializer):
    class Meta:
        model = SystemSettings
        fields = [
            'timezone',
            'max_feeds_capacity_kg',
            'feeder_low_threshold_pct',
            'feeder_high_threshold_pct',
            'water_low_threshold_pct',
            'water_high_threshold_pct',
            'alert_feeder_low_threshold_pct',
            'alert_feeder_high_threshold_pct',
            'alert_water_low_threshold_pct',
            'alert_water_high_threshold_pct',
            'important_log_keywords',
            'alert_recipients',
            'smtp_email_user',
            'max_feeds_capacity_updated_at',
            'max_feeds_capacity_updated_by',
            'updated_at',
        ]
        read_only_fields = ['max_feeds_capacity_updated_at', 'max_feeds_capacity_updated_by', 'updated_at']

    def validate_timezone(self, value):
        try:
            ZoneInfo(value)
            return value
        except Exception as exc:
            raise serializers.ValidationError('Invalid timezone identifier.') from exc

    def validate_max_feeds_capacity_kg(self, value):
        if value is None:
            raise serializers.ValidationError('Max feeds capacity is required.')
        if value <= 0:
            raise serializers.ValidationError('Max feeds capacity must be greater than 0.')
        return value

    def validate_feeder_low_threshold_pct(self, value):
        if value < 0 or value > 100:
            raise serializers.ValidationError('Feeder low threshold must be between 0 and 100.')
        return value

    def validate_feeder_high_threshold_pct(self, value):
        if value < 0 or value > 100:
            raise serializers.ValidationError('Feeder high threshold must be between 0 and 100.')
        return value

    def validate_water_low_threshold_pct(self, value):
        if value < 0 or value > 100:
            raise serializers.ValidationError('Water low threshold must be between 0 and 100.')
        return value

    def validate_water_high_threshold_pct(self, value):
        if value < 0 or value > 100:
            raise serializers.ValidationError('Water high threshold must be between 0 and 100.')
        return value

    def validate_alert_feeder_low_threshold_pct(self, value):
        if value < 0 or value > 100:
            raise serializers.ValidationError('Alert feeder low threshold must be between 0 and 100.')
        return value

    def validate_alert_feeder_high_threshold_pct(self, value):
        if value < 0 or value > 100:
            raise serializers.ValidationError('Alert feeder high threshold must be between 0 and 100.')
        return value

    def validate_alert_water_low_threshold_pct(self, value):
        if value < 0 or value > 100:
            raise serializers.ValidationError('Alert water low threshold must be between 0 and 100.')
        return value

    def validate_alert_water_high_threshold_pct(self, value):
        if value < 0 or value > 100:
            raise serializers.ValidationError('Alert water high threshold must be between 0 and 100.')
        return value

    def validate_important_log_keywords(self, value):
        if not isinstance(value, list):
            raise serializers.ValidationError('Important log keywords must be a list.')

        normalized = []
        for item in value:
            if not isinstance(item, str):
                raise serializers.ValidationError('Each keyword must be a string.')
            token = item.strip().lower()
            if not token:
                continue
            normalized.append(token)

        # Preserve order while removing duplicates.
        deduped = list(dict.fromkeys(normalized))
        if len(deduped) > 50:
            raise serializers.ValidationError('Maximum of 50 keywords allowed.')
        return deduped

    def validate_alert_recipients(self, value):
        if not isinstance(value, list):
            raise serializers.ValidationError('Alert recipients must be a list.')

        normalized = []
        for item in value:
            if not isinstance(item, str):
                raise serializers.ValidationError('Each recipient must be an email string.')
            email = item.strip().lower()
            if not email:
                continue
            try:
                validate_email(email)
            except DjangoValidationError as exc:
                raise serializers.ValidationError(f'Invalid recipient email: {email}') from exc
            normalized.append(email)

        deduped = list(dict.fromkeys(normalized))
        if len(deduped) > 100:
            raise serializers.ValidationError('Maximum of 100 alert recipients allowed.')
        return deduped

    def validate_smtp_email_user(self, value):
        return (value or '').strip()


class DeviceSensorStateSerializer(serializers.ModelSerializer):
    class Meta:
        model = DeviceSensorState
        fields = ['feeder_level_pct', 'water_level_pct', 'last_reported_at', 'updated_at']
        read_only_fields = ['updated_at']


class FeedNowCommandSerializer(serializers.ModelSerializer):
    class Meta:
        model = FeedNowCommand
        fields = [
            'id',
            'device',
            'amount_kg',
            'status',
            'created_at',
            'updated_at',
            'requested_by',
            'executed_at',
            'failure_reason',
        ]
        read_only_fields = ['id', 'device', 'status', 'created_at', 'updated_at', 'requested_by', 'executed_at', 'failure_reason']

    def validate_amount_kg(self, value):
        if value is None:
            raise serializers.ValidationError('amount_kg is required.')
        if value <= 0:
            raise serializers.ValidationError('amount_kg must be greater than 0.')
        return value
