from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0012_merge_20260331_0358'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='important_log_keywords',
            field=models.JSONField(default=list),
        ),
    ]
