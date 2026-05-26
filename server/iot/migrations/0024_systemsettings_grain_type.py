from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0023_log_log_category'),
    ]

    operations = [
        migrations.AddField(
            model_name='systemsettings',
            name='grain_type',
            field=models.CharField(
                choices=[
                    ('pellet_small', 'Pellet Small'),
                    ('pellet_large', 'Pellet Large'),
                    ('crumble', 'Crumble'),
                    ('mash', 'Mash'),
                ],
                default='pellet_small',
                max_length=32,
            ),
        ),
    ]