from django.db import migrations


def default_grain_types():
    return [
        {'grain_type': 'mash', 'feed_ms_per_kg': 137820.8},
        {'grain_type': 'crumbles', 'feed_ms_per_kg': 7000.0},
        {'grain_type': 'mini_pellets', 'feed_ms_per_kg': 4200.0},
        {'grain_type': 'standard_pellets', 'feed_ms_per_kg': 5200.0},
        {'grain_type': 'large_pellets', 'feed_ms_per_kg': 6100.0},
    ]


def _coerce_grain_types(value):
    if not isinstance(value, list):
        return default_grain_types()

    normalized = []
    for item in value:
        if not isinstance(item, dict):
            continue
        grain_type = str(item.get('grain_type') or '').strip()
        if not grain_type:
            continue
        try:
            feed_ms_per_kg = float(item.get('feed_ms_per_kg'))
        except (TypeError, ValueError):
            feed_ms_per_kg = None
        normalized.append({'grain_type': grain_type, 'feed_ms_per_kg': feed_ms_per_kg})

    return normalized or default_grain_types()


def _coerce_index(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _find_index(grain_types, grain_type):
    target = str(grain_type or '').strip()
    if not target:
        return None
    for index, item in enumerate(_coerce_grain_types(grain_types)):
        if str(item.get('grain_type') or '') == target:
            return index
    return None


def _normalize_config(config, grain_types=None):
    normalized = dict(config or {}) if isinstance(config, dict) else {}
    resolved_grain_types = _coerce_grain_types(grain_types if grain_types is not None else normalized.get('grain_types'))
    selected_index = _coerce_index(normalized.get('grain_type_index'))
    selected_name = str(normalized.get('grain_type') or '').strip()

    if selected_index is not None and 0 <= selected_index < len(resolved_grain_types):
        selected_item = resolved_grain_types[selected_index]
    else:
        selected_index = _find_index(resolved_grain_types, selected_name)
        selected_item = resolved_grain_types[selected_index] if selected_index is not None else resolved_grain_types[0]
        if selected_index is None:
            selected_index = 0

    if not selected_name:
        selected_name = str(selected_item.get('grain_type') or '').strip()
    if not selected_name and resolved_grain_types:
        selected_name = str(resolved_grain_types[0].get('grain_type') or '').strip()

    try:
        selected_rate = float(selected_item.get('feed_ms_per_kg'))
    except (TypeError, ValueError):
        selected_rate = None

    if selected_rate is None:
        selected_rate = normalized.get('feed_ms_per_kg')
        try:
            selected_rate = float(selected_rate)
        except (TypeError, ValueError):
            selected_rate = None

    normalized['grain_types'] = resolved_grain_types
    normalized['grain_type_index'] = selected_index
    normalized['grain_type'] = selected_name
    normalized['feed_ms_per_kg'] = selected_rate
    return normalized


def forward(apps, schema_editor):
    DeviceConfig = apps.get_model('iot', 'DeviceConfig')
    SystemSettings = apps.get_model('iot', 'SystemSettings')
    settings_obj = SystemSettings.objects.filter(pk=1).first()
    effective_grain_types = _coerce_grain_types(getattr(settings_obj, 'grain_types', None) if settings_obj else None)

    for config_row in DeviceConfig.objects.all().only('id', 'config'):
        normalized = _normalize_config(config_row.config, grain_types=effective_grain_types)
        if normalized != (config_row.config or {}):
            DeviceConfig.objects.filter(pk=config_row.pk).update(config=normalized)


def backward(apps, schema_editor):
    DeviceConfig = apps.get_model('iot', 'DeviceConfig')
    for config_row in DeviceConfig.objects.all().only('id', 'config'):
        cfg = config_row.config if isinstance(config_row.config, dict) else {}
        if 'grain_type_index' not in cfg:
            continue
        normalized = dict(cfg)
        normalized.pop('grain_type_index', None)
        DeviceConfig.objects.filter(pk=config_row.pk).update(config=normalized)


class Migration(migrations.Migration):

    dependencies = [
        ('iot', '0029_adminrolevote'),
    ]

    operations = [
        migrations.RunPython(forward, backward),
    ]
