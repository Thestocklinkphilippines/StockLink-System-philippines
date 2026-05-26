import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def utc_iso_now():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace('+00:00', 'Z')


def parse_iso(value):
    if not value:
        return 0
    try:
        dt = datetime.fromisoformat(str(value).replace('Z', '+00:00'))
        return int(dt.timestamp())
    except Exception:
        return 0


def schedule_key(schedule):
    if not isinstance(schedule, dict):
        return ''
    if schedule.get('id') is not None:
        return f"id:{schedule.get('id')}"

    name = str(schedule.get('schedule_name') or '').strip().lower()
    t = str(schedule.get('time') or '').strip()
    days = schedule.get('days') or []
    if isinstance(days, list):
        days_norm = ','.join(sorted(str(d).strip() for d in days))
    else:
        days_norm = str(days)
    return f"name:{name}|time:{t}|days:{days_norm}"


def merge_schedules(local_schedules, server_schedules):
    merged = {}
    local_changed = False

    for item in (local_schedules or []):
        if isinstance(item, dict):
            k = schedule_key(item)
            if k:
                merged[k] = item

    for server_item in (server_schedules or []):
        if not isinstance(server_item, dict):
            continue
        k = schedule_key(server_item)
        if not k:
            continue
        local_item = merged.get(k)
        if local_item is None:
            merged[k] = server_item
            continue

        server_ts = parse_iso(server_item.get('last_updated'))
        local_ts = parse_iso(local_item.get('last_updated'))
        if server_ts >= local_ts:
            merged[k] = server_item
        else:
            local_changed = True

    for local_item in (local_schedules or []):
        if not isinstance(local_item, dict):
            continue
        k = schedule_key(local_item)
        if not k:
            continue
        if k not in merged:
            merged[k] = local_item
            local_changed = True

    merged_list = list(merged.values())
    merged_list.sort(key=lambda x: (str(x.get('time') or ''), str(x.get('schedule_name') or '')))
    return merged_list, local_changed


def normalize_schedule_input(schedules):
    out = []
    now_iso = utc_iso_now()
    for item in schedules:
        if not isinstance(item, dict):
            continue
        copy = dict(item)
        copy.setdefault('enabled', True)
        copy.setdefault('days', ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'])
        copy.setdefault('time', '08:00')
        copy.setdefault('feeding_amount_kg', 0.25)
        copy.setdefault('last_updated', now_iso)
        out.append(copy)
    return out


def http_json(method, url, token=None, payload=None, timeout=10):
    body = None
    headers = {'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = f'Token {token}'
    if payload is not None:
        body = json.dumps(payload).encode('utf-8')

    request = Request(url=url, method=method, headers=headers, data=body)
    try:
        with urlopen(request, timeout=timeout) as response:
            text = response.read().decode('utf-8')
            parsed = json.loads(text) if text else {}
            return response.status, parsed
    except HTTPError as exc:
        text = exc.read().decode('utf-8') if exc.fp else ''
        parsed = None
        try:
            parsed = json.loads(text) if text else {}
        except json.JSONDecodeError:
            parsed = {'raw': text}
        return exc.code, parsed
    except URLError as exc:
        return 0, {'detail': str(exc)}


def load_state(path):
    if not path.exists():
        return {
            'last_updated': utc_iso_now(),
            'updated_by': 'esp32-sim',
            'schedules': [],
            'max_feeds_capacity_kg': 1.0,
            'feeder_low_threshold_pct': 20.0,
            'feeder_high_threshold_pct': 80.0,
            'water_low_threshold_pct': 20.0,
            'water_high_threshold_pct': 80.0,
            'low_battery_shutdown_v': 10.0,
            'max_feeds_capacity_updated_at': utc_iso_now(),
            'max_feeds_capacity_updated_by': 'esp32-sim',
            'feeder_level_pct': 100.0,
            'water_level_pct': 100.0,
        }

    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except Exception:
        return {
            'last_updated': utc_iso_now(),
            'updated_by': 'esp32-sim',
            'schedules': [],
            'max_feeds_capacity_kg': 1.0,
            'feeder_low_threshold_pct': 20.0,
            'feeder_high_threshold_pct': 80.0,
            'water_low_threshold_pct': 20.0,
            'water_high_threshold_pct': 80.0,
            'low_battery_shutdown_v': 10.0,
            'max_feeds_capacity_updated_at': utc_iso_now(),
            'max_feeds_capacity_updated_by': 'esp32-sim',
            'feeder_level_pct': 100.0,
            'water_level_pct': 100.0,
        }


def save_state(path, state):
    path.write_text(json.dumps(state, indent=2), encoding='utf-8')


def clamp_pct(value):
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        return 0.0
    return max(0.0, min(100.0, numeric))


def sync_once(
    base_url,
    device_id,
    token,
    state_path,
    set_capacity=None,
    set_schedules=None,
    set_feeder_threshold=None,
    set_water_threshold=None,
    set_feeder_level=None,
    set_water_level=None,
    feeder_decay_per_cycle=0.0,
    water_decay_per_cycle=0.0,
    heartbeat=True,
):
    config_url = f'{base_url}/api/device/{device_id}/config/'
    logs_url = f'{base_url}/api/device/{device_id}/logs/'

    state = load_state(state_path)

    changed_local = False

    # Pull current server copy (mirrors ESP32 polling behavior).
    get_status, get_body = http_json('GET', config_url, token=token)
    if get_status == 200 and isinstance(get_body, dict):
        server_config = get_body.get('config') or {}
        server_last_updated = get_body.get('last_updated')

        merged_schedules, local_schedules_newer = merge_schedules(
            state.get('schedules') or [],
            server_config.get('schedules') or [],
        )
        state['schedules'] = merged_schedules

        if server_last_updated and not local_schedules_newer:
            state['last_updated'] = server_last_updated

        for key in (
            'system_timezone',
            'max_feeds_capacity_kg',
            'feeder_low_threshold_pct',
            'feeder_high_threshold_pct',
            'water_low_threshold_pct',
            'water_high_threshold_pct',
            'low_battery_shutdown_v',
            'max_feeds_capacity_updated_at',
            'max_feeds_capacity_updated_by',
        ):
            if key in server_config:
                state[key] = server_config[key]

        changed_local = local_schedules_newer
    else:
        print(f'[WARN] GET config failed: {get_status} -> {get_body}')

    # Optionally simulate device-side capacity update.
    if set_capacity is not None:
        state['max_feeds_capacity_kg'] = float(set_capacity)
        state['max_feeds_capacity_updated_at'] = utc_iso_now()
        state['max_feeds_capacity_updated_by'] = 'esp32-sim'
        changed_local = True

    if set_feeder_threshold is not None:
        state['feeder_low_threshold_pct'] = float(set_feeder_threshold)
        changed_local = True

    if set_water_threshold is not None:
        state['water_low_threshold_pct'] = float(set_water_threshold)
        changed_local = True

    # Optionally simulate sensor level readings.
    if set_feeder_level is not None:
        state['feeder_level_pct'] = clamp_pct(set_feeder_level)

    if set_water_level is not None:
        state['water_level_pct'] = clamp_pct(set_water_level)

    # Auto-decay levels every cycle to mimic sensor consumption while testing.
    feeder_current = clamp_pct(state.get('feeder_level_pct', 100.0))
    water_current = clamp_pct(state.get('water_level_pct', 100.0))
    state['feeder_level_pct'] = clamp_pct(feeder_current - max(0.0, feeder_decay_per_cycle))
    state['water_level_pct'] = clamp_pct(water_current - max(0.0, water_decay_per_cycle))

    # Optionally replace local schedules using JSON text/file input.
    if set_schedules is not None:
        parsed = None
        source = set_schedules.strip()
        try:
            source_path = Path(source)
            if source_path.exists():
                parsed = json.loads(source_path.read_text(encoding='utf-8'))
            else:
                parsed = json.loads(source)
        except Exception as exc:
            print(f'[WARN] Failed to parse --set-schedules-json input: {exc}')

        if isinstance(parsed, list):
            state['schedules'] = normalize_schedule_input(parsed)
            changed_local = True
        elif parsed is not None:
            print('[WARN] --set-schedules-json must be a JSON array of schedules')

    if changed_local:
        state['last_updated'] = utc_iso_now()
        state['updated_by'] = 'esp32-sim'

    if changed_local:
        post_payload = {
            'config': state,
            'last_updated': state.get('last_updated') or utc_iso_now(),
            'updated_by': 'esp32-sim',
        }
        post_status, post_body = http_json('POST', config_url, token=token, payload=post_payload)

        if post_status in (200, 201):
            print(f'[OK] Config sync: {post_status}')
            response_config = (post_body or {}).get('config') or {}
            for key in (
                'max_feeds_capacity_kg',
                'feeder_low_threshold_pct',
                'water_low_threshold_pct',
                'max_feeds_capacity_updated_at',
                'max_feeds_capacity_updated_by',
            ):
                if key in response_config:
                    state[key] = response_config[key]
            if isinstance(post_body, dict) and post_body.get('last_updated'):
                state['last_updated'] = post_body['last_updated']
        elif post_status == 409:
            print(f'[INFO] Config not pushed (server copy newer): {post_body}')
            server_config = (post_body or {}).get('server_config') or {}
            if isinstance(server_config, dict):
                state['last_updated'] = server_config.get('last_updated') or state.get('last_updated')
                config = server_config.get('config') or {}
                if isinstance(config, dict):
                    merged_schedules, _ = merge_schedules(state.get('schedules') or [], config.get('schedules') or [])
                    state['schedules'] = merged_schedules
        else:
            print(f'[WARN] Config sync failed: {post_status} -> {post_body}')
    else:
        print('[INFO] Config unchanged locally; skipping config POST')

    # Report sensor levels to server (simulated feeder/water percentages)
    sensor_url = f'{base_url}/api/device/{device_id}/sensor-state/'
    sensor_payload = {
        'feeder_level_pct': state.get('feeder_level_pct', 100.0),
        'water_level_pct': state.get('water_level_pct', 100.0),
        'timestamp': utc_iso_now(),
    }
    sensor_status, sensor_body = http_json('POST', sensor_url, token=token, payload=sensor_payload)
    if sensor_status in (200, 201):
        print(f'[OK] Sensor state reported: {sensor_status} (feeder={state.get("feeder_level_pct")}%, water={state.get("water_level_pct")}%)')
    else:
        print(f'[WARN] Sensor report failed: {sensor_status} -> {sensor_body}')

    if heartbeat:
        log_payload = {
            'log_type': 'heartbeat',
            'payload': {
                'source': 'esp32-sim',
                'at': utc_iso_now(),
                'max_feeds_capacity_kg': state.get('max_feeds_capacity_kg'),
                'feeder_low_threshold_pct': state.get('feeder_low_threshold_pct'),
                'feeder_high_threshold_pct': state.get('feeder_high_threshold_pct'),
                'water_low_threshold_pct': state.get('water_low_threshold_pct'),
                'water_high_threshold_pct': state.get('water_high_threshold_pct'),
                'feeder_level_pct': state.get('feeder_level_pct'),
                'water_level_pct': state.get('water_level_pct'),
            },
        }
        log_status, log_body = http_json('POST', logs_url, token=token, payload=log_payload)
        if log_status in (200, 201):
            print(f'[OK] Heartbeat log sent: {log_status}')
        else:
            print(f'[WARN] Heartbeat failed: {log_status} -> {log_body}')

    save_state(state_path, state)


def main():
    parser = argparse.ArgumentParser(description='Simulate an ESP32 device pinging/syncing with the Django server.')
    parser.add_argument('--base-url', default='http://127.0.0.1:8000', help='Server base URL (default: http://127.0.0.1:8000)')
    parser.add_argument('--device-id', default='esp32-001', help='Device ID to simulate')
    parser.add_argument('--token', required=True, help='Device auth token (Authorization: Token <token>)')
    parser.add_argument('--interval', type=int, default=10, help='Sync interval seconds (default: 10)')
    parser.add_argument('--once', action='store_true', help='Run one cycle only')
    parser.add_argument('--set-max-capacity', type=float, default=None, help='Simulate device writing max_feeds_capacity_kg')
    parser.add_argument('--set-feeder-threshold', type=float, default=None, help='Simulate device writing feeder_low_threshold_pct')
    parser.add_argument('--set-water-threshold', type=float, default=None, help='Simulate device writing water_low_threshold_pct')
    parser.add_argument('--set-feeder-level', type=float, default=None, help='Simulate feeder tank level %% (0-100)')
    parser.add_argument('--set-water-level', type=float, default=None, help='Simulate water tank level %% (0-100)')
    parser.add_argument('--decay-feeder-per-cycle', type=float, default=0.0, help='Decrease feeder level %% by this amount every sync cycle')
    parser.add_argument('--decay-water-per-cycle', type=float, default=0.0, help='Decrease water level %% by this amount every sync cycle')
    parser.add_argument('--set-schedules-json', default=None, help='JSON array or path to JSON file to replace local schedules')
    parser.add_argument('--no-heartbeat', action='store_true', help='Skip heartbeat log posts')
    parser.add_argument('--state-file', default='.esp32_sim_state.json', help='Path to local simulator state file')
    args = parser.parse_args()

    state_path = Path(args.state_file)

    print('ESP32 simulator started')
    print(f'Base URL: {args.base_url}')
    print(f'Device ID: {args.device_id}')
    print(f'State file: {state_path}')

    while True:
        sync_once(
            base_url=args.base_url.rstrip('/'),
            device_id=args.device_id,
            token=args.token,
            state_path=state_path,
            set_capacity=args.set_max_capacity,
            set_schedules=args.set_schedules_json,
            set_feeder_threshold=args.set_feeder_threshold,
            set_water_threshold=args.set_water_threshold,
            set_feeder_level=args.set_feeder_level,
            set_water_level=args.set_water_level,
            feeder_decay_per_cycle=args.decay_feeder_per_cycle,
            water_decay_per_cycle=args.decay_water_per_cycle,
            heartbeat=not args.no_heartbeat,
        )
        if args.once:
            break
        time.sleep(max(1, args.interval))

    return 0


if __name__ == '__main__':
    sys.exit(main())
