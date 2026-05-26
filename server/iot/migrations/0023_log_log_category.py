from django.db import migrations, models


def add_log_category_column(apps, schema_editor):
    Log = apps.get_model('iot', 'Log')
    table_name = Log._meta.db_table

    with schema_editor.connection.cursor() as cursor:
        existing_columns = {
            column.name for column in schema_editor.connection.introspection.get_table_description(cursor, table_name)
        }

    if 'log_category' in existing_columns:
        return

    field = models.CharField(max_length=32, default='system')
    field.set_attributes_from_name('log_category')
    schema_editor.add_field(Log, field)


def remove_log_category_column(apps, schema_editor):
    Log = apps.get_model('iot', 'Log')
    table_name = Log._meta.db_table

    with schema_editor.connection.cursor() as cursor:
        existing_columns = {
            column.name for column in schema_editor.connection.introspection.get_table_description(cursor, table_name)
        }

    if 'log_category' not in existing_columns:
        return

    field = models.CharField(max_length=32, default='system')
    field.set_attributes_from_name('log_category')
    schema_editor.remove_field(Log, field)


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0022_systemsettings_low_battery_shutdown_v'),
    ]

    operations = [
        migrations.SeparateDatabaseAndState(
            database_operations=[
                migrations.RunPython(add_log_category_column, remove_log_category_column),
            ],
            state_operations=[
                migrations.AddField(
                    model_name='log',
                    name='log_category',
                    field=models.CharField(default='system', max_length=32),
                ),
            ],
        ),
    ]