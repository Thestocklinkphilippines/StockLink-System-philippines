import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0015_systemsettings_alert_recipients'),
    ]

    operations = [
        migrations.CreateModel(
            name='FeedNowCommand',
            fields=[
                ('id', models.BigAutoField(auto_created=True, primary_key=True, serialize=False, verbose_name='ID')),
                ('amount_kg', models.FloatField()),
                ('status', models.CharField(choices=[('pending', 'Pending'), ('executed', 'Executed'), ('failed', 'Failed')], default='pending', max_length=16)),
                ('created_at', models.DateTimeField(auto_now_add=True)),
                ('updated_at', models.DateTimeField(auto_now=True)),
                ('requested_by', models.CharField(blank=True, default='', max_length=150)),
                ('executed_at', models.DateTimeField(blank=True, null=True)),
                ('failure_reason', models.CharField(blank=True, default='', max_length=255)),
                ('device', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, related_name='feed_now_commands', to='iot.device')),
            ],
            options={
                'ordering': ['-created_at', '-id'],
            },
        ),
    ]
