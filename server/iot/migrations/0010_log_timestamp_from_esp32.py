# Migration to update Log model to use ESP32-provided timestamps

from django.db import migrations, models
from django.utils import timezone


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0009_alter_alert_timestamp'),
    ]

    operations = [
        # Remove auto_now_add from timestamp and set default to now for existing rows
        migrations.AlterField(
            model_name='log',
            name='timestamp',
            field=models.DateTimeField(default=timezone.now),
        ),
    ]
