from rest_framework import serializers
from .models import Device, DeviceConfig, Alert, Log, DeviceSensorState
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
        fields = ['id', 'device', 'alert_type', 'timestamp', 'resolved']

class LogSerializer(serializers.ModelSerializer):
    class Meta:
        model = Log
        fields = ['id', 'device', 'log_type', 'payload', 'timestamp']


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


class DeviceSensorStateSerializer(serializers.ModelSerializer):
    class Meta:
        model = DeviceSensorState
        fields = ['feeder_level_pct', 'water_level_pct', 'last_reported_at', 'updated_at']
        read_only_fields = ['updated_at']
