# Migration to add refresh tracking to Log model

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0010_log_timestamp_from_esp32'),
    ]

    operations = [
        migrations.AddField(
            model_name='log',
            name='refresh_count',
            field=models.IntegerField(default=1),
        ),
        migrations.AddField(
            model_name='log',
            name='last_updated',
            field=models.DateTimeField(auto_now=True),
        ),
    ]
