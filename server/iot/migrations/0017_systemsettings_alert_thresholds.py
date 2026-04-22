from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0016_feednowcommand'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='alert_feeder_high_threshold_pct',
            field=models.FloatField(default=80.0),
        ),
        migrations.AddField(
            model_name='systemsettings',
            name='alert_feeder_low_threshold_pct',
            field=models.FloatField(default=20.0),
        ),
        migrations.AddField(
            model_name='systemsettings',
            name='alert_water_high_threshold_pct',
            field=models.FloatField(default=80.0),
        ),
        migrations.AddField(
            model_name='systemsettings',
            name='alert_water_low_threshold_pct',
            field=models.FloatField(default=20.0),
        ),
    ]
