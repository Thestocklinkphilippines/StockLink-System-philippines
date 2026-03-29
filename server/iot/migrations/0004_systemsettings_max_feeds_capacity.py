from django.db import migrations, models
import django.utils.timezone


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0003_systemsettings'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='max_feeds_capacity_kg',
            field=models.FloatField(default=1.0),
        ),
        migrations.AddField(
            model_name='systemsettings',
            name='max_feeds_capacity_updated_at',
            field=models.DateTimeField(default=django.utils.timezone.now),
        ),
        migrations.AddField(
            model_name='systemsettings',
            name='max_feeds_capacity_updated_by',
            field=models.CharField(default='server', max_length=16),
        ),
    ]
