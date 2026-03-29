# Generated migration for device sensor state

from django.db import migrations, models
import django.db.models.deletion


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0006_systemsettings_high_thresholds'),
    ]

    operations = [
        migrations.CreateModel(
            name='DeviceSensorState',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('feeder_level_pct', models.FloatField(default=100.0)),
                ('water_level_pct', models.FloatField(default=100.0)),
                ('last_reported_at', models.DateTimeField(blank=True, null=True)),
                ('updated_at', models.DateTimeField(auto_now=True)),
                ('device', models.OneToOneField(on_delete=django.db.models.deletion.CASCADE, related_name='sensor_state', to='iot.device')),
            ],
        ),
    ]
