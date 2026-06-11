from django.db import migrations


def remove_server_keypad_calibration(apps, schema_editor):
    DeviceConfig = apps.get_model('iot', 'DeviceConfig')
    for device_config in DeviceConfig.objects.all().iterator():
        config = dict(device_config.config or {})
        if 'keypad_calibration' not in config:
            continue
        config.pop('keypad_calibration', None)
        device_config.config = config
        device_config.save(update_fields=['config'])


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0032_alert_resolved_at'),
    ]

    operations = [
        migrations.RunPython(remove_server_keypad_calibration, migrations.RunPython.noop),
    ]
