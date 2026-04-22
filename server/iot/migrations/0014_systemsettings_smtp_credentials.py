from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0013_systemsettings_important_log_keywords'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='smtp_email_password',
            field=models.CharField(blank=True, default='', max_length=255),
        ),
        migrations.AddField(
            model_name='systemsettings',
            name='smtp_email_user',
            field=models.CharField(blank=True, default='', max_length=255),
        ),
    ]
