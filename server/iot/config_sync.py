import re

from django.db import transaction

from .models import Schedule, SystemSettings


_TIME_RE = re.compile(r"^(\d{2}):(\d{2})$")
_VALID_DAYS = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}
_DAY_MAP = {
    "sunday": "Sun",
    "monday": "Mon",
    "tuesday": "Tue",
    "wednesday": "Wed",
    "thursday": "Thu",
    "friday": "Fri",
    "saturday": "Sat",
}


def _coerce_bool(value):
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        token = value.strip().lower()
        if token in {"1", "true", "yes", "on"}:
            return True
        if token in {"0", "false", "no", "off"}:
            return False
    return False


def _coerce_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _coerce_time(value):
    token = str(value or "").strip()
    match = _TIME_RE.match(token)
    if not match:
        return None
    hh = int(match.group(1))
    mm = int(match.group(2))
    if hh < 0 or hh > 23 or mm < 0 or mm > 59:
        return None
    return f"{hh:02d}:{mm:02d}"


def _normalize_day(value):
    token = str(value or "").strip()
    if not token:
        return None
    short = token[:3].title()
    if short in _VALID_DAYS:
        return short
    return _DAY_MAP.get(token.lower())


def _coerce_days(values):
    if not isinstance(values, list):
        return []
    normalized = []
    for item in values:
        day = _normalize_day(item)
        if day and day not in normalized:
            normalized.append(day)
    return normalized


def sync_thresholds_from_payload(payload):
    if not isinstance(payload, dict):
        return False

    settings_obj = SystemSettings.get_solo()
    changed = False
    updated_fields = set()
    fields = [
        "feeder_low_threshold_pct",
        "feeder_high_threshold_pct",
        "water_low_threshold_pct",
        "water_high_threshold_pct",
        "alert_feeder_low_threshold_pct",
        "alert_feeder_high_threshold_pct",
        "alert_water_low_threshold_pct",
        "alert_water_high_threshold_pct",
        "low_battery_shutdown_v",
    ]

    for field in fields:
        if field not in payload:
            continue
        value = _coerce_float(payload.get(field))
        if value is None or value <= 0 or value > 100:
            continue
        if getattr(settings_obj, field) != value:
            setattr(settings_obj, field, value)
            changed = True
        updated_fields.add(field)

    if changed:
        settings_obj._changed_system_setting_fields = updated_fields
        settings_obj.save()
    return changed


def sync_schedules_from_payload(device, payload):
    if not isinstance(payload, dict) or "schedules" not in payload:
        return False

    schedules_payload = payload.get("schedules")
    if not isinstance(schedules_payload, list):
        return False

    parsed_items = []
    invalid_items = 0
    for idx, raw in enumerate(schedules_payload):
        if not isinstance(raw, dict):
            invalid_items += 1
            continue

        amount = _coerce_float(raw.get("feeding_amount_kg"))
        sched_time = _coerce_time(raw.get("time"))
        if amount is None or amount <= 0 or sched_time is None:
            invalid_items += 1
            continue

        schedule_name = str(raw.get("schedule_name") or f"Schedule {idx + 1}").strip()[:128] or f"Schedule {idx + 1}"
        days = _coerce_days(raw.get("days") or [])
        enabled = _coerce_bool(raw.get("enabled", True))

        schedule_id = raw.get("id")
        try:
            schedule_id = int(schedule_id) if schedule_id is not None else None
        except (TypeError, ValueError):
            schedule_id = None

        parsed_items.append(
            {
                "id": schedule_id,
                "schedule_name": schedule_name,
                "enabled": enabled,
                "days": days,
                "time": sched_time,
                "feeding_amount_kg": amount,
            }
        )

    # Avoid destructive updates when payload is clearly incomplete/invalid.
    if schedules_payload and not parsed_items:
        return False

    with transaction.atomic():
        existing = {s.id: s for s in Schedule.objects.filter(device=device)}
        keep_ids = set()
        changed = False

        for item in parsed_items:
            schedule_id = item.get("id")
            if schedule_id and schedule_id in existing:
                sched = existing[schedule_id]
                mutated = False
                for key in ("schedule_name", "enabled", "days", "time", "feeding_amount_kg"):
                    if getattr(sched, key) != item[key]:
                        setattr(sched, key, item[key])
                        mutated = True
                if mutated:
                    sched.save()
                    changed = True
                keep_ids.add(sched.id)
            else:
                created = Schedule.objects.create(device=device, **{k: item[k] for k in ("schedule_name", "enabled", "days", "time", "feeding_amount_kg")})
                keep_ids.add(created.id)
                changed = True

        if invalid_items == 0:
            deleted_count, _ = Schedule.objects.filter(device=device).exclude(id__in=keep_ids).delete()
            if deleted_count > 0:
                changed = True

    return changed
