# Generated migration for high thresholds

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0005_systemsettings_thresholds'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='feeder_high_threshold_pct',
            field=models.FloatField(default=80.0),
        ),
        migrations.AddField(
            model_name='systemsettings',
            name='water_high_threshold_pct',
            field=models.FloatField(default=80.0),
        ),
    ]
