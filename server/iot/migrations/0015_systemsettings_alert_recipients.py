from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0014_systemsettings_smtp_credentials'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='alert_recipients',
            field=models.JSONField(default=list),
        ),
    ]
