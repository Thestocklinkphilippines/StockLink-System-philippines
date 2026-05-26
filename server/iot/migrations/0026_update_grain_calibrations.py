from django.db import migrations, models


def default_grain_types():
    return [
        {'grain_type': 'mash', 'feed_ms_per_kg': 137820.8},
        {'grain_type': 'crumbles', 'feed_ms_per_kg': 7000.0},
        {'grain_type': 'mini_pellets', 'feed_ms_per_kg': 4200.0},
        {'grain_type': 'standard_pellets', 'feed_ms_per_kg': 5200.0},
        {'grain_type': 'large_pellets', 'feed_ms_per_kg': 6100.0},
    ]


def update_settings_rows(apps, schema_editor):
    SystemSettings = apps.get_model('iot', 'SystemSettings')
    new_types = default_grain_types()
    valid_types = {item['grain_type'] for item in new_types}

    for settings_obj in SystemSettings.objects.all():
        settings_obj.grain_types = new_types
        if settings_obj.grain_type not in valid_types:
            settings_obj.grain_type = 'standard_pellets'
        settings_obj.save(update_fields=['grain_types', 'grain_type'])


def revert_settings_rows(apps, schema_editor):
    SystemSettings = apps.get_model('iot', 'SystemSettings')
    old_types = [
        {'grain_type': 'pellet_small', 'feed_ms_per_kg': 4200.0},
        {'grain_type': 'pellet_large', 'feed_ms_per_kg': 5200.0},
        {'grain_type': 'crumble', 'feed_ms_per_kg': 6100.0},
        {'grain_type': 'mash', 'feed_ms_per_kg': 7000.0},
    ]
    valid_types = {item['grain_type'] for item in old_types}

    for settings_obj in SystemSettings.objects.all():
        settings_obj.grain_types = old_types
        if settings_obj.grain_type not in valid_types:
            settings_obj.grain_type = 'pellet_large'
        settings_obj.save(update_fields=['grain_types', 'grain_type'])


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0025_systemsettings_grain_types'),
    ]

    operations = [
        migrations.AlterField(
            model_name='systemsettings',
            name='grain_type',
            field=models.CharField(default='standard_pellets', max_length=32),
        ),
        migrations.AlterField(
            model_name='systemsettings',
            name='grain_types',
            field=models.JSONField(default=default_grain_types),
        ),
        migrations.RunPython(update_settings_rows, revert_settings_rows),
    ]