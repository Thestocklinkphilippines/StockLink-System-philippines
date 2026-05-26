from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0021_alert_payload'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='low_battery_shutdown_v',
            field=models.FloatField(default=10.0),
        ),
    ]