# Generated migration to add refresh tracking to Alert model

from django.db import migrations, models
from django.utils import timezone


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0007_devicesensorstate'),
    ]

    operations = [
        migrations.AddField(
            model_name='alert',
            name='refresh_count',
            field=models.IntegerField(default=1),
        ),
        migrations.AddField(
            model_name='alert',
            name='last_updated',
            field=models.DateTimeField(auto_now=True),
        ),
        migrations.AlterField(
            model_name='alert',
            name='timestamp',
            field=models.DateTimeField(default=timezone.now),
        ),
    ]
