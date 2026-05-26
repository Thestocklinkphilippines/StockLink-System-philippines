from django.db import migrations, models


def default_grain_types():
    return [
        {'grain_type': 'pellet_small', 'feed_ms_per_kg': 4200.0},
        {'grain_type': 'pellet_large', 'feed_ms_per_kg': 5200.0},
        {'grain_type': 'crumble', 'feed_ms_per_kg': 6100.0},
        {'grain_type': 'mash', 'feed_ms_per_kg': 7000.0},
    ]


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0024_systemsettings_grain_type'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='grain_types',
            field=models.JSONField(default=default_grain_types),
        ),
    ]
