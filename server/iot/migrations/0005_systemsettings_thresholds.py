from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0004_systemsettings_max_feeds_capacity'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='feeder_low_threshold_pct',
            field=models.FloatField(default=20.0),
        ),
        migrations.AddField(
            model_name='systemsettings',
            name='water_low_threshold_pct',
            field=models.FloatField(default=20.0),
        ),
    ]
