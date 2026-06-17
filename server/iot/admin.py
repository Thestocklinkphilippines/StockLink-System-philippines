from django.contrib import admin
from django.contrib.auth import get_user_model
from django.contrib.auth.admin import UserAdmin as DjangoUserAdmin
from .models import Device, DeviceConfig, Alert, Log, DeviceEvent, SystemSettings, DeviceSensorState, FeedNowCommand, UserApproval, AdminRoleVote
from .models import Schedule
from .config_sync import sync_schedules_from_payload, sync_thresholds_from_payload
from django.utils.html import format_html
import json
from django import forms

User = get_user_model()

try:
    admin.site.unregister(User)
except admin.sites.NotRegistered:
    pass


@admin.register(User)
class UserAdmin(DjangoUserAdmin):
    list_display = ('username', 'email', 'is_staff', 'is_active', 'is_superuser', 'last_login')
    list_filter = ('is_staff', 'is_active', 'is_superuser', 'groups')
    search_fields = ('username', 'email', 'first_name', 'last_name')
    ordering = ('username',)
    fieldsets = (
        (None, {'fields': ('username', 'password')}),
        ('Personal info', {'fields': ('first_name', 'last_name', 'email')}),
        ('Access', {'fields': ('is_active', 'is_staff', 'is_superuser', 'groups', 'user_permissions')}),
        ('Important dates', {'fields': ('last_login', 'date_joined')}),
    )
    add_fieldsets = (
        (None, {
            'classes': ('wide',),
            'fields': ('username', 'email', 'password1', 'password2', 'is_active', 'is_staff', 'is_superuser'),
        }),
    )


@admin.register(UserApproval)
class UserApprovalAdmin(admin.ModelAdmin):
    list_display = ('user', 'is_approved', 'approved_by', 'approved_at', 'rejected', 'created_at')
    search_fields = ('user__username', 'user__email')
    readonly_fields = ('created_at',)


@admin.register(AdminRoleVote)
class AdminRoleVoteAdmin(admin.ModelAdmin):
    list_display = ('target_user', 'voter', 'vote_type', 'created_at')
    search_fields = ('target_user__username', 'voter__username')
    list_filter = ('vote_type',)
    readonly_fields = ('created_at',)

# weekday choices for admin form
WEEKDAY_CHOICES = [
    ('Sun', 'Sunday'),
    ('Mon', 'Monday'),
    ('Tue', 'Tuesday'),
    ('Wed', 'Wednesday'),
    ('Thu', 'Thursday'),
    ('Fri', 'Friday'),
    ('Sat', 'Saturday'),
]


@admin.register(Device)
class DeviceAdmin(admin.ModelAdmin):
    list_display = ('device_id', 'auth_token', 'last_seen', 'connection_status')
    search_fields = ('device_id',)


@admin.register(DeviceConfig)
class DeviceConfigAdmin(admin.ModelAdmin):
    list_display = ('device', 'last_updated', 'updated_by', 'schedule_count')
    readonly_fields = ('last_updated','pretty_config')
    search_fields = ('device__device_id',)

    def pretty_config(self, obj):
        try:
            cfg = obj.config or {}
            pretty = json.dumps(cfg, indent=2, sort_keys=True)
            return format_html('<pre style="white-space:pre-wrap; max-width:80ch;">{}</pre>', pretty)
        except Exception:
            return '(invalid JSON)'
    pretty_config.short_description = 'Config (JSON)'

    def schedule_count(self, obj):
        try:
            cfg = obj.config or {}
            schedules = cfg.get('schedules') if isinstance(cfg, dict) else None
            return len(schedules) if schedules else 0
        except Exception:
            return 0
    schedule_count.short_description = 'Schedules'

    def save_model(self, request, obj, form, change):
        super().save_model(request, obj, form, change)
        payload = obj.config if isinstance(obj.config, dict) else {}
        sync_thresholds_from_payload(payload)
        sync_schedules_from_payload(obj.device, payload)


@admin.register(FeedNowCommand)
class FeedNowCommandAdmin(admin.ModelAdmin):
    list_display = ('id', 'device', 'amount_kg', 'status', 'requested_by', 'created_at', 'executed_at')
    list_filter = ('status',)
    search_fields = ('device__device_id', 'requested_by', 'failure_reason')
    readonly_fields = ('created_at', 'updated_at', 'executed_at')
    ordering = ('-created_at', '-id')


@admin.register(Alert)
class AlertAdmin(admin.ModelAdmin):
    list_display = ('device', 'alert_type', 'timestamp', 'resolved_at', 'last_updated', 'refresh_count', 'resolved')
    list_filter = ('alert_type', 'resolved')
    search_fields = ('device__device_id', 'alert_type')
    ordering = ('-last_updated', '-id')


@admin.register(Log)
class LogAdmin(admin.ModelAdmin):
    list_display = ('device', 'log_type', 'timestamp', 'last_updated', 'refresh_count')
    list_filter = ('log_type',)
    search_fields = ('device__device_id', 'log_type')
    ordering = ('-last_updated', '-id')


@admin.register(DeviceEvent)
class DeviceEventAdmin(admin.ModelAdmin):
    list_display = ('device', 'event_type', 'event_id', 'occurred_at', 'received_at', 'delivery_status')
    list_filter = ('event_type', 'delivery_status', 'source')
    search_fields = ('device__device_id', 'event_id', 'event_type', 'boot_id')
    ordering = ('-occurred_at', '-received_at', '-id')


@admin.register(Schedule)
class ScheduleAdmin(admin.ModelAdmin):
    list_display = ('schedule_name', 'device', 'time', 'feeding_amount_kg', 'enabled', 'days_display', 'last_updated')
    list_filter = ('enabled',)
    search_fields = ('schedule_name', 'device__device_id')

    class ScheduleForm(forms.ModelForm):
        days = forms.MultipleChoiceField(choices=WEEKDAY_CHOICES, widget=forms.CheckboxSelectMultiple, required=False)

        class Meta:
            model = Schedule
            fields = '__all__'

        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            # Initialize the days field from the JSONField value
            if self.instance and getattr(self.instance, 'days', None) is not None:
                try:
                    self.fields['days'].initial = list(self.instance.days)
                except Exception:
                    self.fields['days'].initial = []

        def clean_days(self):
            val = self.cleaned_data.get('days') or []
            # ensure stored as list of strings
            return list(val)

        def save(self, commit=True):
            inst = super().save(commit=False)
            inst.days = self.cleaned_data.get('days') or []
            if commit:
                inst.save()
            return inst

    form = ScheduleForm

    def days_display(self, obj):
        try:
            return ', '.join(obj.days) if obj.days else '-'
        except Exception:
            return '-'
    days_display.short_description = 'Days'


@admin.register(SystemSettings)
class SystemSettingsAdmin(admin.ModelAdmin):
    class SystemSettingsForm(forms.ModelForm):
        grain_types_text = forms.CharField(
            label='Grain types',
            required=False,
            widget=forms.Textarea(attrs={'rows': 8}),
            help_text='JSON array of grain type objects with grain_type and feed_ms_per_kg.',
        )
        important_log_keywords_text = forms.CharField(
            label='Important log keywords',
            required=False,
            widget=forms.Textarea(attrs={'rows': 4}),
            help_text='Enter comma-separated keywords, for example: critical, error, offline. JSON arrays are also accepted.',
        )

        class Meta:
            model = SystemSettings
            fields = '__all__'

        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            grain_types = getattr(self.instance, 'grain_types', None) or []
            if isinstance(grain_types, list):
                self.fields['grain_types_text'].initial = json.dumps(grain_types, indent=2)
            keywords = getattr(self.instance, 'important_log_keywords', None) or []
            if isinstance(keywords, list):
                self.fields['important_log_keywords_text'].initial = ', '.join(str(item) for item in keywords)

        def clean_grain_types_text(self):
            raw_value = (self.cleaned_data.get('grain_types_text') or '').strip()
            if not raw_value:
                return []

            try:
                parsed = json.loads(raw_value)
            except Exception as exc:
                raise forms.ValidationError('Invalid JSON array.') from exc

            if not isinstance(parsed, list):
                raise forms.ValidationError('JSON input must be an array of grain type objects.')

            normalized = []
            for item in parsed:
                if not isinstance(item, dict):
                    raise forms.ValidationError('Each grain type must be an object.')

                grain_type = str(item.get('grain_type') or '').strip()
                if not grain_type:
                    raise forms.ValidationError('Each grain type must include grain_type.')

                try:
                    feed_ms_per_kg = float(item.get('feed_ms_per_kg'))
                except (TypeError, ValueError) as exc:
                    raise forms.ValidationError(f'Invalid feed_ms_per_kg for {grain_type}.') from exc

                if feed_ms_per_kg <= 0:
                    raise forms.ValidationError(f'feed_ms_per_kg must be greater than 0 for {grain_type}.')

                normalized.append({
                    'grain_type': grain_type,
                    'feed_ms_per_kg': feed_ms_per_kg,
                })

            return normalized

        def clean_important_log_keywords_text(self):
            raw_value = (self.cleaned_data.get('important_log_keywords_text') or '').strip()
            if not raw_value:
                return []

            if raw_value.startswith('['):
                try:
                    parsed = json.loads(raw_value)
                except Exception as exc:
                    raise forms.ValidationError('Invalid JSON array.') from exc

                if not isinstance(parsed, list):
                    raise forms.ValidationError('JSON input must be an array of strings.')
                keywords = [str(item).strip().lower() for item in parsed if str(item).strip()]
            else:
                keywords = [item.strip().lower() for item in raw_value.split(',') if item.strip()]

            return list(dict.fromkeys(keywords))

        def save(self, commit=True):
            instance = super().save(commit=False)
            instance.grain_types = self.cleaned_data.get('grain_types_text') or []
            instance.important_log_keywords = self.cleaned_data.get('important_log_keywords_text') or []
            if commit:
                instance.save()
            return instance

    list_display = (
        'timezone',
        'max_feeds_capacity_kg',
        'grain_type',
        'feeder_low_threshold_pct',
        'feeder_high_threshold_pct',
        'water_low_threshold_pct',
        'water_high_threshold_pct',
        'alert_feeder_low_threshold_pct',
        'alert_feeder_high_threshold_pct',
        'alert_water_low_threshold_pct',
        'alert_water_high_threshold_pct',
        'low_battery_shutdown_v',
        'smtp_email_user',
        'max_feeds_capacity_updated_at',
        'max_feeds_capacity_updated_by',
        'updated_at',
    )

    fieldsets = (
        ('System', {'fields': ('timezone', 'important_log_keywords_text')}),
        ('Refill Control Thresholds', {
            'fields': (
                'max_feeds_capacity_kg',
                'grain_type',
                'grain_types_text',
                'feeder_low_threshold_pct',
                'feeder_high_threshold_pct',
                'water_low_threshold_pct',
                'water_high_threshold_pct',
                'low_battery_shutdown_v',
            )
        }),
        ('Alert Trigger Thresholds', {
            'fields': (
                'alert_feeder_low_threshold_pct',
                'alert_feeder_high_threshold_pct',
                'alert_water_low_threshold_pct',
                'alert_water_high_threshold_pct',
            )
        }),
        ('SMTP Configuration', {
            'fields': (
                'smtp_email_user',
                'smtp_email_password',
            )
        }),
        ('Audit', {
            'fields': (
                'max_feeds_capacity_updated_at',
                'max_feeds_capacity_updated_by',
                'updated_at',
            )
        }),
    )
    readonly_fields = ('updated_at',)
    form = SystemSettingsForm

    def save_model(self, request, obj, form, change):
        if form and 'max_feeds_capacity_kg' in form.changed_data:
            from django.utils import timezone
            obj.max_feeds_capacity_updated_at = timezone.now()
            obj.max_feeds_capacity_updated_by = 'admin'
        super().save_model(request, obj, form, change)


@admin.register(DeviceSensorState)
class DeviceSensorStateAdmin(admin.ModelAdmin):
    list_display = ('device', 'feeder_level_pct', 'water_level_pct', 'water_current_liters', 'last_reported_at', 'updated_at')
    readonly_fields = ('updated_at',)
    search_fields = ('device__device_id',)
