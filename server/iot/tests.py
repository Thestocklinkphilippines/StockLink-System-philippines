import json
import re
from datetime import timedelta
from io import StringIO

from django.contrib.auth.models import User
from django.core import mail
from django.test import Client, TestCase, override_settings
from django.core.management import call_command
from django.utils import timezone

from .models import Alert, Device, DeviceConfig, DeviceEvent, DeviceSensorState, FeedNowCommand, Log, Schedule, SystemSettings, UserApproval, default_grain_types, normalize_grain_config


class OfflineReplayIngestTests(TestCase):
    def setUp(self):
        self.device = Device.objects.create(device_id='esp32-001', auth_token='token-123')
        self.client = Client()
        self.auth_headers = {'HTTP_AUTHORIZATION': f'Token {self.device.auth_token}'}

    def _post_json(self, path, payload):
        return self.client.post(
            path,
            data=json.dumps(payload),
            content_type='application/json',
            **self.auth_headers,
        )

    def test_logs_duplicate_event_id_is_ignored(self):
        payload = {
            'log_type': 'feeding',
            'timestamp': '2026-05-16T10:00:00Z',
            'event_id': 'evt-feed-1',
            'boot_id': 'boot-a',
            'sequence': 1,
            'source': 'esp32',
            'payload': {
                'amount_kg': 0.25,
                'remaining_kg': 0.75,
            },
        }

        first = self._post_json('/api/device/esp32-001/logs/', payload)
        second = self._post_json('/api/device/esp32-001/logs/', payload)

        self.assertEqual(first.status_code, 201)
        self.assertEqual(second.status_code, 200)
        log = Log.objects.get(device=self.device, log_type='feeding')
        self.assertEqual(log.log_category, 'feeding')
        self.assertEqual(Log.objects.filter(device=self.device, log_type='feeding').count(), 1)
        self.assertEqual(DeviceEvent.objects.filter(device=self.device).count(), 1)

    def test_manual_feed_logs_stack_as_history_rows(self):
        first_payload = {
            'log_type': 'feed_now',
            'timestamp': '2026-05-16T10:00:00Z',
            'event_id': 'evt-feed-now-1',
            'source': 'esp32',
            'payload': {
                'status': 'executed',
                'command_id': 70,
                'amount_kg': 0.214,
                'remaining_kg': 7.0,
            },
        }
        second_payload = {
            'log_type': 'feed_now',
            'timestamp': '2026-05-16T10:05:00Z',
            'event_id': 'evt-feed-now-2',
            'source': 'esp32',
            'payload': {
                'status': 'executed',
                'command_id': 71,
                'amount_kg': 0.214,
                'remaining_kg': 6.786,
            },
        }

        first = self._post_json('/api/device/esp32-001/logs/', first_payload)
        second = self._post_json('/api/device/esp32-001/logs/', second_payload)

        self.assertEqual(first.status_code, 201)
        self.assertEqual(second.status_code, 201)
        self.assertEqual(Log.objects.filter(device=self.device, log_type='feed_now').count(), 2)
        self.assertEqual(DeviceEvent.objects.filter(device=self.device, event_type='feed_now').count(), 2)

    def test_logs_endpoint_returns_all_feed_history_rows(self):
        base_timestamp = timezone.now()
        Log.objects.bulk_create(
            [
                Log(
                    device=self.device,
                    log_type='feeding',
                    log_category='feeding',
                    payload={'amount_kg': 0.25, 'remaining_kg': 0.75, 'index': index},
                    timestamp=base_timestamp - timedelta(minutes=index),
                )
                for index in range(205)
            ]
        )

        user = User.objects.create_user(username='logsviewer', password='pass12345')
        client = Client()
        client.force_login(user)

        response = client.get('/api/device/esp32-001/logs/')

        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.json()), 205)

    def test_manual_feeding_log_keeps_manual_trigger(self):
        payload = {
            'log_type': 'feeding',
            'timestamp': '2026-05-16T10:00:00Z',
            'event_id': 'manual-feed-1',
            'payload': {
                'amount_kg': 0.623,
                'remaining_kg': 1.552,
                'feeder_level_pct': 31.0,
                'trigger': 'manual',
                'source': 'control_panel',
            },
        }

        response = self._post_json('/api/device/esp32-001/logs/', payload)

        self.assertEqual(response.status_code, 201)
        log = Log.objects.get(device=self.device, log_type='feeding')
        self.assertEqual(log.payload['trigger'], 'manual')
        self.assertEqual(log.payload['feed_type'], 'manual')
        self.assertEqual(log.payload['source'], 'control_panel')

    def test_scheduled_feeding_log_keeps_schedule_metadata(self):
        payload = {
            'log_type': 'feeding',
            'timestamp': '2026-05-16T11:30:00Z',
            'event_id': 'scheduled-feed-1',
            'payload': {
                'amount_kg': 0.25,
                'remaining_kg': 1.75,
                'feeder_level_pct': 87.5,
                'trigger': 'schedule',
                'schedule_id': 12,
                'schedule_name': 'Morning feed',
                'schedule_time': '11:30',
            },
        }

        response = self._post_json('/api/device/esp32-001/logs/', payload)

        self.assertEqual(response.status_code, 201)
        log = Log.objects.get(device=self.device, log_type='feeding')
        self.assertEqual(log.payload['trigger'], 'schedule')
        self.assertEqual(log.payload['feed_type'], 'scheduled')
        self.assertEqual(log.payload['schedule_id'], 12)
        self.assertEqual(log.payload['schedule_name'], 'Morning feed')

    def test_legacy_feeding_log_without_trigger_is_unknown(self):
        payload = {
            'log_type': 'feeding',
            'timestamp': '2026-05-16T12:00:00Z',
            'event_id': 'legacy-feed-1',
            'payload': {
                'amount_kg': 0.1,
                'remaining_kg': 1.9,
            },
        }

        response = self._post_json('/api/device/esp32-001/logs/', payload)

        self.assertEqual(response.status_code, 201)
        log = Log.objects.get(device=self.device, log_type='feeding')
        self.assertEqual(log.payload['trigger'], 'unknown')
        self.assertEqual(log.payload['feed_type'], 'unknown')

    def test_batch_events_duplicate_and_sensor_projection(self):
        batch = {
            'events': [
                {
                    'event_type': 'feeding',
                    'timestamp': '2026-05-16T10:00:00Z',
                    'event_id': 'batch-feed-1',
                    'boot_id': 'boot-a',
                    'sequence': 10,
                    'source': 'esp32',
                    'payload': {
                        'amount_kg': 0.25,
                        'remaining_kg': 0.75,
                    },
                },
                {
                    'event_type': 'feeding',
                    'timestamp': '2026-05-16T10:00:00Z',
                    'event_id': 'batch-feed-1',
                    'boot_id': 'boot-a',
                    'sequence': 10,
                    'source': 'esp32',
                    'payload': {
                        'amount_kg': 0.25,
                        'remaining_kg': 0.75,
                    },
                },
                {
                    'event_type': 'sensor_state',
                    'occurred_at': '2026-05-16T10:01:00Z',
                    'event_id': 'batch-sensor-1',
                    'boot_id': 'boot-a',
                    'sequence': 11,
                    'source': 'esp32',
                    'feeder_level_pct': 88.0,
                    'water_level_pct': 91.0,
                    'battery_voltage_v': 3.78,
                    'feed_sufficient': True,
                    'feed_current_kg': 6.5,
                    'feed_required_next_kg': 1.2,
                },
            ]
        }

        response = self._post_json('/api/device/esp32-001/events/', batch)
        self.assertEqual(response.status_code, 200)
        results = response.json()['results']
        self.assertEqual(results[0]['accepted'], True)
        self.assertEqual(results[1]['duplicate'], True)
        self.assertEqual(results[2]['projection'], 'sensor_state')
        self.assertEqual(DeviceEvent.objects.filter(device=self.device).count(), 2)
        self.assertEqual(Log.objects.filter(device=self.device, log_type='feeding').count(), 1)

        sensor_state = DeviceSensorState.objects.get(device=self.device)
        self.assertEqual(sensor_state.feeder_level_pct, 88.0)
        self.assertEqual(sensor_state.water_level_pct, 91.0)
        self.assertEqual(sensor_state.battery_voltage_v, 3.78)
        self.assertTrue(sensor_state.feed_sufficient)
        self.assertEqual(sensor_state.feed_current_kg, 6.5)
        self.assertEqual(sensor_state.feed_required_next_kg, 1.2)

    def test_sensor_state_post_persists_feed_sufficiency_fields(self):
        payload = {
            'feeder_level_pct': 74.0,
            'water_level_pct': 81.0,
            'battery_voltage_v': 3.77,
            'feed_sufficient': False,
            'feed_current_kg': 0.4,
            'feed_required_next_kg': 1.0,
            'timestamp': '2026-05-16T10:05:00Z',
            'event_id': 'sensor-direct-1',
        }

        response = self._post_json('/api/device/esp32-001/sensor-state/', payload)

        self.assertEqual(response.status_code, 200)
        body = response.json()
        self.assertEqual(body['status'], 'updated')
        self.assertEqual(body['feed_sufficient'], False)
        self.assertEqual(body['feed_current_kg'], 0.4)
        self.assertEqual(body['feed_required_next_kg'], 1.0)

        sensor_state = DeviceSensorState.objects.get(device=self.device)
        self.assertFalse(sensor_state.feed_sufficient)
        self.assertEqual(sensor_state.feed_current_kg, 0.4)
        self.assertEqual(sensor_state.feed_required_next_kg, 1.0)

    def test_sensor_state_derives_feed_sufficiency_when_omitted(self):
        settings_obj = SystemSettings.get_solo()
        settings_obj.max_feeds_capacity_kg = 5.0
        settings_obj.save(update_fields=['max_feeds_capacity_kg', 'updated_at'])
        DeviceConfig.objects.create(
            device=self.device,
            config={'max_feeds_capacity_kg': 5.0},
        )
        Schedule.objects.create(
            device=self.device,
            schedule_name='Next feed',
            enabled=True,
            days=[],
            time='23:59',
            feeding_amount_kg=0.2,
        )
        DeviceSensorState.objects.create(
            device=self.device,
            feeder_level_pct=0.0,
            water_level_pct=0.0,
            feed_sufficient=False,
            feed_current_kg=0.0,
            feed_required_next_kg=0.2,
        )
        payload = {
            'feeder_level_pct': 34.84395981,
            'water_level_pct': 78.73400116,
            'timestamp': '2026-06-07T17:37:15Z',
            'event_id': 'sensor-derived-feed-1',
        }

        response = self._post_json('/api/device/esp32-001/sensor-state/', payload)

        self.assertEqual(response.status_code, 200)
        body = response.json()
        self.assertTrue(body['feed_sufficient'])
        self.assertAlmostEqual(body['feed_current_kg'], 1.7421979905)
        self.assertAlmostEqual(body['feed_required_next_kg'], 0.2)

        sensor_state = DeviceSensorState.objects.get(device=self.device)
        self.assertTrue(sensor_state.feed_sufficient)
        self.assertAlmostEqual(sensor_state.feed_current_kg, 1.7421979905)
        self.assertAlmostEqual(sensor_state.feed_required_next_kg, 0.2)

    def test_feed_now_ack_is_idempotent(self):
        command = FeedNowCommand.objects.create(device=self.device, amount_kg=0.25)
        payload = {'status': 'executed', 'reason': 'ok', 'event_id': 'ack-1'}

        first = self._post_json(f'/api/device/esp32-001/feed-now/{command.id}/ack/', payload)
        second = self._post_json(f'/api/device/esp32-001/feed-now/{command.id}/ack/', payload)

        self.assertEqual(first.status_code, 200)
        self.assertEqual(second.status_code, 200)
        command.refresh_from_db()
        self.assertEqual(command.status, FeedNowCommand.STATUS_EXECUTED)
        self.assertEqual(DeviceEvent.objects.filter(device=self.device, event_type='feed_now_ack').count(), 1)

    def test_feeding_log_with_feed_now_trigger_executes_command(self):
        command = FeedNowCommand.objects.create(device=self.device, amount_kg=0.15)
        payload = {
            'log_type': 'feeding',
            'timestamp': '2026-05-29T12:34:20Z',
            'event_id': 'feed-now-completion-81',
            'source': 'esp32',
            'payload': {
                'amount_kg': 0.15,
                'remaining_kg': 0.0,
                'feeder_level_pct': 0,
                'trigger': 'feed_now',
                'command_id': command.id,
                'phase': 'completed',
            },
        }

        user = User.objects.create_user(username='feed-now-reader', password='pass12345')
        client = Client()
        client.force_login(user)

        response = self._post_json('/api/device/esp32-001/logs/', payload)

        self.assertEqual(response.status_code, 201)
        command.refresh_from_db()
        self.assertEqual(command.status, FeedNowCommand.STATUS_EXECUTED)
        self.assertIsNotNone(command.executed_at)

        feed_now_list = client.get(f'/api/device/{self.device.device_id}/feed-now/')
        self.assertEqual(feed_now_list.status_code, 200)
        self.assertEqual(feed_now_list.json()[0]['id'], command.id)
        self.assertEqual(feed_now_list.json()[0]['status'], FeedNowCommand.STATUS_EXECUTED)

    def test_pending_feed_now_command_is_reconciled_from_existing_logs(self):
        command = FeedNowCommand.objects.create(device=self.device, amount_kg=0.15)
        Log.objects.create(
            device=self.device,
            log_type='feeding',
            log_category='feeding',
            timestamp=timezone.now(),
            payload={
                'amount_kg': 0.15,
                'remaining_kg': 0.0,
                'feeder_level_pct': 0,
                'trigger': 'feed_now',
                'command_id': command.id,
                'phase': 'completed',
            },
        )

        user = User.objects.create_user(username='feed-now-backfill', password='pass12345')
        user.is_staff = True
        user.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=user, is_approved=True)

        client = Client()
        client.force_login(user)

        feed_now_list = client.get(f'/api/device/{self.device.device_id}/feed-now/')
        config_res = client.get(f'/api/device/{self.device.device_id}/config/')

        self.assertEqual(feed_now_list.status_code, 200)
        self.assertEqual(feed_now_list.json()[0]['id'], command.id)
        self.assertEqual(feed_now_list.json()[0]['status'], FeedNowCommand.STATUS_EXECUTED)
        self.assertEqual(config_res.status_code, 200)
        self.assertIsNone(config_res.json()['config']['feed_now_command'])
        command.refresh_from_db()
        self.assertEqual(command.status, FeedNowCommand.STATUS_EXECUTED)

    def test_feed_now_command_status_is_shared_between_config_and_list(self):
        user = User.objects.create_user(username='staff-feed-now', password='pass12345')
        user.is_staff = True
        user.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=user, is_approved=True)

        user_client = Client()
        user_client.force_login(user)

        command = FeedNowCommand.objects.create(device=self.device, amount_kg=0.25)

        config_before = user_client.get(f'/api/device/{self.device.device_id}/config/')
        list_before = user_client.get(f'/api/device/{self.device.device_id}/feed-now/')

        self.assertEqual(config_before.status_code, 200)
        self.assertEqual(list_before.status_code, 200)
        self.assertIsNotNone(config_before.json()['config']['feed_now_command'])
        self.assertEqual(config_before.json()['config']['feed_now_command']['id'], command.id)
        self.assertEqual(config_before.json()['config']['feed_now_command']['status'], FeedNowCommand.STATUS_PENDING)
        self.assertEqual(list_before.json()[0]['id'], command.id)
        self.assertEqual(list_before.json()[0]['status'], FeedNowCommand.STATUS_PENDING)

        ack_response = self._post_json(
            f'/api/device/esp32-001/feed-now/{command.id}/ack/',
            {'status': 'executed', 'reason': 'ok', 'event_id': 'ack-shared-1'},
        )

        self.assertEqual(ack_response.status_code, 200)

        config_after = user_client.get(f'/api/device/{self.device.device_id}/config/')
        list_after = user_client.get(f'/api/device/{self.device.device_id}/feed-now/')

        self.assertEqual(config_after.status_code, 200)
        self.assertIsNone(config_after.json()['config']['feed_now_command'])
        self.assertEqual(list_after.status_code, 200)
        self.assertEqual(list_after.json()[0]['id'], command.id)
        self.assertEqual(list_after.json()[0]['status'], FeedNowCommand.STATUS_EXECUTED)

    def test_device_watermark_closes_stale_pending_feed_now_command(self):
        command = FeedNowCommand.objects.create(device=self.device, amount_kg=0.25)
        cfg = DeviceConfig.objects.create(
            device=self.device,
            config={
                'last_feed_now_command_id': command.id,
                'manual_feed_snapshot_id': command.id,
                'feed_now_command': {'id': command.id, 'amount_kg': 0.25},
            },
        )

        response = self.client.get(
            f'/api/device/{self.device.device_id}/config/',
            **self.auth_headers,
        )

        self.assertEqual(response.status_code, 200)
        self.assertIsNone(response.json()['config']['feed_now_command'])
        command.refresh_from_db()
        self.assertEqual(command.status, FeedNowCommand.STATUS_FAILED)
        self.assertIn('watermark advanced', command.failure_reason)
        cfg.refresh_from_db()
        self.assertNotIn('feed_now_command', cfg.config)

    def test_non_staff_user_can_queue_feed_now_command(self):
        user = User.objects.create_user(username='regular-feed-now', password='pass12345')
        UserApproval.objects.create(user=user, is_approved=True)

        client = Client()
        client.force_login(user)

        response = client.post(
            f'/api/device/{self.device.device_id}/feed-now/',
            data=json.dumps({'amount_kg': 0.25}),
            content_type='application/json',
        )

        self.assertEqual(response.status_code, 201)
        self.assertEqual(FeedNowCommand.objects.filter(device=self.device).count(), 1)
        self.assertEqual(FeedNowCommand.objects.get(device=self.device).requested_by, user.username)

    def test_connection_timeout_creates_alert_and_heartbeat_restores(self):
        stale_seen = timezone.now() - timedelta(seconds=61)
        self.device.last_seen = stale_seen
        self.device.connection_status = 'connected'
        self.device.save(update_fields=['last_seen', 'connection_status'])

        call_command('check_device_connections', timeout_seconds=60, stdout=StringIO())

        self.device.refresh_from_db()
        self.assertEqual(self.device.connection_status, 'disconnected')
        self.assertEqual(Alert.objects.filter(device=self.device, alert_type='device_connection_loss').count(), 1)
        self.assertEqual(Log.objects.filter(device=self.device, log_type='device_connection_loss').count(), 1)

        heartbeat_payload = {
            'log_type': 'heartbeat',
            'timestamp': timezone.now().isoformat(),
            'event_id': 'heartbeat-restore-1',
            'boot_id': 'boot-a',
            'sequence': 99,
            'source': 'esp32',
            'payload': {
                'uptime_ms': 123456,
            },
        }
        response = self._post_json('/api/device/esp32-001/logs/', heartbeat_payload)

        self.assertEqual(response.status_code, 201)
        self.device.refresh_from_db()
        self.assertEqual(self.device.connection_status, 'connected')
        self.assertEqual(Alert.objects.filter(device=self.device, alert_type='device_connection_restored').count(), 1)
        self.assertEqual(Log.objects.filter(device=self.device, log_type='device_connection_restored').count(), 1)

    def test_device_list_exposes_connection_status_metadata(self):
        self.device.last_seen = timezone.now() - timedelta(seconds=10)
        self.device.connection_status = 'connected'
        self.device.save(update_fields=['last_seen', 'connection_status'])
        user = User.objects.create_user(username='device-status-viewer', password='pass12345')
        self.client.force_login(user)

        response = self.client.get('/api/devices/')

        self.assertEqual(response.status_code, 200)
        body = response.json()
        self.assertEqual(body['devices'], ['esp32-001'])
        status_payload = body['device_statuses'][0]
        self.assertEqual(status_payload['device_id'], 'esp32-001')
        self.assertEqual(status_payload['connection_status'], 'connected')
        self.assertTrue(status_payload['is_online'])
        self.assertEqual(status_payload['connection_timeout_seconds'], 60)
        self.assertIsNotNone(status_payload['last_seen'])

    def test_config_response_exposes_effective_disconnected_status_when_stale(self):
        self.device.last_seen = timezone.now() - timedelta(seconds=90)
        self.device.connection_status = 'connected'
        self.device.save(update_fields=['last_seen', 'connection_status'])
        DeviceConfig.objects.create(device=self.device, config={})
        user = User.objects.create_user(username='config-status-viewer', password='pass12345')
        self.client.force_login(user)

        response = self.client.get(f'/api/device/{self.device.device_id}/config/')

        self.assertEqual(response.status_code, 200)
        status_payload = response.json()['device_status']
        self.assertEqual(status_payload['stored_connection_status'], 'connected')
        self.assertEqual(status_payload['connection_status'], 'disconnected')
        self.assertFalse(status_payload['is_online'])
        self.assertGreaterEqual(status_payload['seconds_since_last_seen'], 60)

    def test_shutdown_log_creates_low_battery_shutdown_alert(self):
        payload = {
            'log_type': 'shutdown',
            'timestamp': timezone.now().isoformat(),
            'event_id': 'shutdown-1',
            'boot_id': 'boot-a',
            'sequence': 500,
            'source': 'esp32',
            'payload': {
                'battery_voltage_v': 9.87,
                'shutdown_threshold_v': 10.0,
                'buffered_event_count': 3,
                'requested_at': '2026-05-18T12:00:00Z',
            },
        }

        response = self._post_json('/api/device/esp32-001/logs/', payload)

        self.assertEqual(response.status_code, 201)
        self.assertEqual(Log.objects.filter(device=self.device, log_type='shutdown').count(), 1)
        self.assertEqual(DeviceEvent.objects.filter(device=self.device, event_type='shutdown').count(), 1)

        alert = Alert.objects.get(device=self.device, alert_type='low_battery_shutdown')
        self.assertFalse(alert.resolved)
        self.assertEqual(alert.payload['battery_voltage_v'], 9.87)
        self.assertEqual(alert.payload['shutdown_threshold_v'], 10.0)
        self.assertEqual(alert.payload['buffered_event_count'], 3)

    def test_low_battery_shutdown_resolved_marks_active_alert_resolved(self):
        Alert.objects.create(
            device=self.device,
            alert_type='low_battery_shutdown',
            timestamp=timezone.now() - timedelta(minutes=5),
            payload={
                'battery_voltage_v': 9.72,
                'shutdown_threshold_v': 10.0,
            },
        )

        payload = {
            'alert_type': 'low_battery_shutdown_resolved',
            'payload': {
                'battery_voltage_v': 10.4,
                'resolved_at': '2026-05-18T12:05:00Z',
            },
        }

        response = self._post_json('/api/device/esp32-001/alerts/', payload)

        self.assertEqual(response.status_code, 201)
        active_alert = Alert.objects.get(device=self.device, alert_type='low_battery_shutdown')
        self.assertTrue(active_alert.resolved)
        self.assertEqual(active_alert.payload['battery_voltage_v'], 10.4)
        self.assertEqual(Alert.objects.filter(device=self.device, alert_type='low_battery_shutdown_resolved').count(), 1)

    def test_shutdown_alert_auto_resolves_after_grace_period(self):
        Alert.objects.create(
            device=self.device,
            alert_type='low_battery_shutdown',
            timestamp=timezone.now() - timedelta(seconds=6),
            payload={
                'battery_voltage_v': 9.55,
                'shutdown_threshold_v': 10.0,
            },
        )

        call_command('resolve_shutdown_alerts', grace_seconds=5, stdout=StringIO())

        active_alert = Alert.objects.get(device=self.device, alert_type='low_battery_shutdown')
        self.assertTrue(active_alert.resolved)
        self.assertEqual(Alert.objects.filter(device=self.device, alert_type='low_battery_shutdown_resolved').count(), 1)
        self.assertEqual(Log.objects.filter(device=self.device, log_type='shutdown_resolved').count(), 1)

    def test_device_config_merge_preserves_existing_battery_threshold(self):
        DeviceConfig.objects.create(
            device=self.device,
            config={
                'low_battery_shutdown_v': 9.6,
                'legacy_mode': True,
                'total_feeds_today_kg': 1.25,
            },
        )
        user = User.objects.create_user(username='admin', password='pass12345')
        user.is_staff = True
        user.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=user, is_approved=True)
        client = Client()
        client.force_login(user)

        response = client.post(
            f'/api/device/{self.device.device_id}/config/',
            data=json.dumps({'config': {'feeder_low_threshold_pct': 18.0}}),
            content_type='application/json',
        )

        self.assertEqual(response.status_code, 200)
        cfg = response.json()['config']
        self.assertEqual(cfg['low_battery_shutdown_v'], 9.6)
        self.assertTrue(cfg['legacy_mode'])
        self.assertEqual(cfg['feeder_low_threshold_pct'], 18.0)

    def test_device_config_get_derives_total_feeds_today_from_logs(self):
        today = timezone.localdate().isoformat()
        DeviceConfig.objects.create(
            device=self.device,
            config={
                'total_feeds_today_kg': 0.0,
                'total_feeds_today_date': today,
            },
        )
        now_ts = timezone.now()
        Log.objects.create(
            device=self.device,
            log_type='feeding',
            log_category='feeding',
            timestamp=now_ts - timedelta(minutes=4),
            payload={
                'amount_kg': 0.25,
                'remaining_kg': 1.75,
                'trigger': 'schedule',
            },
        )
        Log.objects.create(
            device=self.device,
            log_type='feed_now',
            log_category='feeding',
            timestamp=now_ts - timedelta(minutes=3),
            payload={
                'amount_kg': 0.15,
                'remaining_kg': 1.60,
                'status': 'executed',
                'command_id': 42,
            },
        )
        Log.objects.create(
            device=self.device,
            log_type='feeding',
            log_category='feeding',
            timestamp=now_ts - timedelta(minutes=2),
            payload={
                'amount_kg': 0.15,
                'remaining_kg': 1.60,
                'trigger': 'feed_now',
                'status': 'executed',
                'command_id': 42,
            },
        )
        Log.objects.create(
            device=self.device,
            log_type='feed_now',
            log_category='feeding',
            timestamp=now_ts - timedelta(minutes=1),
            payload={
                'amount_kg': 0.99,
                'status': 'failed',
                'command_id': 43,
            },
        )

        response = self.client.get(
            f'/api/device/{self.device.device_id}/config/',
            **self.auth_headers,
        )

        self.assertEqual(response.status_code, 200)
        cfg = response.json()['config']
        self.assertEqual(cfg['total_feeds_today_date'], today)
        self.assertAlmostEqual(cfg['total_feeds_today_kg'], 0.40)

    def test_device_config_save_drops_deprecated_water_tank_depth_field(self):
        cfg = DeviceConfig.objects.create(
            device=self.device,
            config={
                'water_tank_full_cm': '12.5',
                'water_tank_depth_cm': '34.75',
                'legacy_mode': True,
            },
        )

        cfg.refresh_from_db()
        self.assertEqual(cfg.config['water_tank_full_cm'], 12.5)
        self.assertNotIn('water_tank_depth_cm', cfg.config)
        self.assertTrue(cfg.config['legacy_mode'])

    def test_device_post_drops_deprecated_water_tank_depth_field(self):
        newer_ts = (timezone.now() + timedelta(seconds=5)).isoformat()
        response = self.client.post(
            f'/api/device/{self.device.device_id}/config/',
            data=json.dumps(
                {
                    'config': {
                        'water_tank_full_cm': '13.25',
                        'water_tank_depth_cm': '39.5',
                        'legacy_mode': True,
                    },
                    'last_updated': newer_ts,
                }
            ),
            content_type='application/json',
            **self.auth_headers,
        )

        self.assertEqual(response.status_code, 200)
        cfg = DeviceConfig.objects.get(device=self.device)
        self.assertEqual(cfg.config['water_tank_full_cm'], 13.25)
        self.assertNotIn('water_tank_depth_cm', cfg.config)
        self.assertTrue(cfg.config['legacy_mode'])
        self.assertEqual(response.json()['config']['water_tank_full_cm'], 13.25)
        self.assertNotIn('water_tank_depth_cm', response.json()['config'])

    def test_system_settings_exposes_battery_shutdown_voltage(self):
        user = User.objects.create_user(username='admin2', password='pass12345')
        user.is_staff = True
        user.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=user, is_approved=True)
        client = Client()
        client.force_login(user)

        response = client.get('/api/system/settings/')
        self.assertEqual(response.status_code, 200)
        self.assertIn('low_battery_shutdown_v', response.json())

        patch_response = client.patch(
            '/api/system/settings/',
            data=json.dumps({'low_battery_shutdown_v': 9.75}),
            content_type='application/json',
        )
        self.assertEqual(patch_response.status_code, 200)
        self.assertEqual(patch_response.json()['low_battery_shutdown_v'], 9.75)
        self.assertEqual(SystemSettings.get_solo().low_battery_shutdown_v, 9.75)

    def test_system_settings_exposes_grain_type(self):
        user = User.objects.create_user(username='admin-grain', password='pass12345')
        user.is_staff = True
        user.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=user, is_approved=True)
        client = Client()
        client.force_login(user)

        response = client.get('/api/system/settings/')
        self.assertEqual(response.status_code, 200)
        self.assertIn('grain_type', response.json())
        self.assertIn('grain_types', response.json())
        self.assertEqual([item['grain_type'] for item in response.json()['grain_types']], ['mash', 'crumbles', 'mini_pellets', 'standard_pellets', 'large_pellets'])

        patch_response = client.patch(
            '/api/system/settings/',
            data=json.dumps({'grain_type': 'large_pellets'}),
            content_type='application/json',
        )
        self.assertEqual(patch_response.status_code, 200)
        self.assertEqual(patch_response.json()['grain_type'], 'large_pellets')
        self.assertEqual(SystemSettings.get_solo().grain_type, 'large_pellets')

    def test_system_settings_update_propagates_battery_voltage_to_device_config(self):
        cfg = DeviceConfig.objects.create(
            device=self.device,
            config={
                'low_battery_shutdown_v': 9.6,
                'legacy_mode': True,
            },
        )
        user = User.objects.create_user(username='admin3', password='pass12345')
        user.is_staff = True
        user.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=user, is_approved=True)
        client = Client()
        client.force_login(user)

        response = client.patch(
            '/api/system/settings/',
            data=json.dumps({'low_battery_shutdown_v': 9.75}),
            content_type='application/json',
        )

        self.assertEqual(response.status_code, 200)
        cfg.refresh_from_db()
        self.assertEqual(cfg.config['low_battery_shutdown_v'], 9.75)
        self.assertTrue(cfg.config['legacy_mode'])

    def test_system_settings_update_propagates_grain_type_to_device_config(self):
        cfg = DeviceConfig.objects.create(
            device=self.device,
            config={
                'grain_type': 'mini_pellets',
                'legacy_mode': True,
            },
        )
        user = User.objects.create_user(username='admin4', password='pass12345')
        user.is_staff = True
        user.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=user, is_approved=True)
        client = Client()
        client.force_login(user)

        response = client.patch(
            '/api/system/settings/',
            data=json.dumps({'grain_type': 'crumbles'}),
            content_type='application/json',
        )

        self.assertEqual(response.status_code, 200)
        cfg.refresh_from_db()
        self.assertEqual(cfg.config['grain_type'], 'crumbles')
        self.assertEqual(cfg.config['feed_ms_per_kg'], 7000.0)
        self.assertIn('grain_types', cfg.config)
        self.assertTrue(cfg.config['legacy_mode'])

    def test_normalize_grain_config_prefers_grain_type_index(self):
        normalized = normalize_grain_config(
            {
                'grain_types': default_grain_types(),
                'grain_type_index': 2,
                'grain_type': 'mash',
                'feed_ms_per_kg': 137820.8,
            }
        )

        self.assertEqual(normalized['grain_type_index'], 2)
        self.assertEqual(normalized['grain_type'], 'mini_pellets')
        self.assertEqual(normalized['feed_ms_per_kg'], 4200.0)

    def test_device_get_returns_normalized_grain_index(self):
        settings_obj = SystemSettings.get_solo()
        settings_obj.grain_type = 'crumbles'
        settings_obj._changed_system_setting_fields = {'grain_type'}
        settings_obj.save(update_fields=['grain_type', 'updated_at'])

        cfg = DeviceConfig.objects.create(
            device=self.device,
            config={
                'grain_type': 'mini_pellets',
                'grain_type_index': 2,
                'feed_ms_per_kg': 137820.8,
                'legacy_mode': True,
            },
        )
        DeviceConfig.objects.filter(pk=cfg.pk).update(
            config={
                'grain_type': 'mini_pellets',
                'grain_type_index': 2,
                'feed_ms_per_kg': 137820.8,
                'legacy_mode': True,
            }
        )

        response = self.client.get(
            f'/api/device/{self.device.device_id}/config/',
            **self.auth_headers,
        )

        self.assertEqual(response.status_code, 200)
        config = response.json()['config']
        self.assertEqual(config['grain_type_index'], 2)
        self.assertEqual(config['grain_type'], 'mini_pellets')
        self.assertEqual(config['feed_ms_per_kg'], 4200.0)
        self.assertIn('grain_types', config)

    def test_device_post_accepts_grain_type_index(self):
        settings_obj = SystemSettings.get_solo()
        settings_obj.grain_type = 'mash'
        settings_obj._changed_system_setting_fields = {'grain_type'}
        settings_obj.save(update_fields=['grain_type', 'updated_at'])

        newer_ts = (timezone.now() + timedelta(seconds=5)).isoformat()
        response = self.client.post(
            f'/api/device/{self.device.device_id}/config/',
            data=json.dumps(
                {
                    'config': {
                        'grain_type_index': 2,
                        'grain_type': 'mini_pellets',
                    },
                    'last_updated': newer_ts,
                }
            ),
            content_type='application/json',
            **self.auth_headers,
        )

        self.assertEqual(response.status_code, 200)
        cfg = DeviceConfig.objects.get(device=self.device)
        self.assertEqual(cfg.config['grain_type_index'], 2)
        self.assertEqual(cfg.config['grain_type'], 'mini_pellets')
        self.assertEqual(cfg.config['feed_ms_per_kg'], 4200.0)
        self.assertEqual(SystemSettings.get_solo().grain_type, 'mash')

    def test_device_post_prefers_grain_type_when_index_conflicts(self):
        newer_ts = (timezone.now() + timedelta(seconds=5)).isoformat()
        response = self.client.post(
            f'/api/device/{self.device.device_id}/config/',
            data=json.dumps(
                {
                    'config': {
                        'grain_type': 'large_pellets',
                        # stale index for 'crumbles'
                        'grain_type_index': 1,
                    },
                    'last_updated': newer_ts,
                }
            ),
            content_type='application/json',
            **self.auth_headers,
        )

        self.assertEqual(response.status_code, 200)
        cfg = DeviceConfig.objects.get(device=self.device)
        self.assertEqual(cfg.config['grain_type'], 'large_pellets')
        self.assertEqual(cfg.config['grain_type_index'], 4)
        self.assertEqual(cfg.config['feed_ms_per_kg'], 6100.0)

    def test_populate_grain_types_backfills_grain_type_index(self):
        cfg = DeviceConfig.objects.create(
            device=self.device,
            config={
                'grain_type': 'crumbles',
                'legacy_mode': True,
            },
        )
        DeviceConfig.objects.filter(pk=cfg.pk).update(
            config={
                'grain_type': 'crumbles',
                'legacy_mode': True,
            }
        )

        call_command('populate_grain_types', stdout=StringIO())

        cfg.refresh_from_db()
        self.assertEqual(cfg.config['grain_type_index'], 1)
        self.assertEqual(cfg.config['grain_type'], 'crumbles')
        self.assertTrue(cfg.config['legacy_mode'])

    def test_device_post_keeps_device_grain_type_when_syncing_max_capacity(self):
        cfg = DeviceConfig.objects.create(
            device=self.device,
            config={
                'grain_type': 'large_pellets',
                'feed_ms_per_kg': 6100.0,
            },
        )

        newer_ts = (timezone.now() + timedelta(seconds=5)).isoformat()
        response = self.client.post(
            f'/api/device/{self.device.device_id}/config/',
            data=json.dumps(
                {
                    'config': {
                        'grain_type': 'large_pellets',
                        'max_feeds_capacity_kg': 11.0,
                        'max_feeds_capacity_updated_at': newer_ts,
                    },
                    'last_updated': newer_ts,
                }
            ),
            content_type='application/json',
            **self.auth_headers,
        )

        self.assertEqual(response.status_code, 200)
        cfg.refresh_from_db()
        self.assertEqual(cfg.config['grain_type'], 'large_pellets')
        self.assertEqual(response.json()['config']['grain_type'], 'large_pellets')

    def test_device_post_does_not_update_system_settings_grain_type(self):
        settings_obj = SystemSettings.get_solo()
        settings_obj.grain_type = 'mash'
        settings_obj._changed_system_setting_fields = {'grain_type'}
        settings_obj.save(update_fields=['grain_type', 'updated_at'])

        newer_ts = (timezone.now() + timedelta(seconds=5)).isoformat()
        response = self.client.post(
            f'/api/device/{self.device.device_id}/config/',
            data=json.dumps(
                {
                    'config': {
                        'grain_type': 'large_pellets',
                    },
                    'last_updated': newer_ts,
                }
            ),
            content_type='application/json',
            **self.auth_headers,
        )

        self.assertEqual(response.status_code, 200)
        settings_obj.refresh_from_db()
        self.assertEqual(settings_obj.grain_type, 'mash')

    def test_schedule_signal_does_not_override_device_grain_type(self):
        settings_obj = SystemSettings.get_solo()
        settings_obj.grain_type = 'mash'
        settings_obj._changed_system_setting_fields = {'grain_type'}
        settings_obj.save(update_fields=['grain_type', 'updated_at'])

        cfg = DeviceConfig.objects.create(
            device=self.device,
            config={
                'grain_type': 'large_pellets',
                'feed_ms_per_kg': 6100.0,
            },
        )
        cfg.save()

        Schedule.objects.create(
            device=self.device,
            schedule_name='signal-check',
            enabled=True,
            days=['Mon'],
            time='06:30',
            feeding_amount_kg=0.25,
        )

        cfg.refresh_from_db()
        self.assertEqual(cfg.config['grain_type'], 'large_pellets')


class AuthAccountTests(TestCase):
    def test_delete_current_user_removes_account_and_logs_out(self):
        user = User.objects.create_user(username='deleteuser', email='delete@example.com', password='StrongPass1!')
        client = Client()
        client.force_login(user)

        response = client.delete('/api/auth/user/')

        self.assertEqual(response.status_code, 200)
        self.assertFalse(User.objects.filter(username='deleteuser').exists())

        current = client.get('/api/auth/user/')
        self.assertEqual(current.status_code, 200)
        self.assertFalse(current.json()['is_authenticated'])


@override_settings(EMAIL_BACKEND='django.core.mail.backends.locmem.EmailBackend')
class AdminRoleManagementTests(TestCase):
    def setUp(self):
        self.admin_user = User.objects.create_user(username='roleadmin', email='roleadmin@example.com', password='StrongPass1!')
        self.admin_user.is_staff = True
        self.admin_user.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=self.admin_user, is_approved=True)

        self.admin_user_2 = User.objects.create_user(username='roleadmin2', email='roleadmin2@example.com', password='StrongPass1!')
        self.admin_user_2.is_staff = True
        self.admin_user_2.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=self.admin_user_2, is_approved=True)

        self.admin_user_3 = User.objects.create_user(username='roleadmin3', email='roleadmin3@example.com', password='StrongPass1!')
        self.admin_user_3.is_staff = True
        self.admin_user_3.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=self.admin_user_3, is_approved=True)

        self.normal_user = User.objects.create_user(username='roleuser', email='roleuser@example.com', password='StrongPass1!')
        UserApproval.objects.create(user=self.normal_user, is_approved=True)

    def test_current_user_exposes_role_fields(self):
        client = Client()
        client.force_login(self.admin_user)

        response = client.get('/api/auth/user/')

        self.assertEqual(response.status_code, 200)
        data = response.json()
        self.assertTrue(data['is_authenticated'])
        self.assertTrue(data['is_staff'])
        self.assertEqual(data['role'], 'ADMIN')
        self.assertEqual(data['username'], 'roleadmin')

    def test_admin_can_promote_and_demote_user(self):
        client = Client()
        client.force_login(self.admin_user)

        promote = client.post(f'/api/admin/users/{self.normal_user.id}/promote/')
        self.assertEqual(promote.status_code, 200)
        self.assertFalse(promote.json().get('action_applied', False))
        self.normal_user.refresh_from_db()
        self.assertFalse(self.normal_user.is_staff)
        self.assertEqual(promote.json()['role'], 'USER')

        second_client = Client()
        second_client.force_login(self.admin_user_2)
        second_vote = second_client.post(f'/api/admin/users/{self.normal_user.id}/promote/')
        self.assertEqual(second_vote.status_code, 200)
        self.assertTrue(second_vote.json().get('action_applied', False))
        self.normal_user.refresh_from_db()
        self.assertTrue(self.normal_user.is_staff)
        self.assertEqual(second_vote.json()['role'], 'ADMIN')

        demote = client.post(f'/api/admin/users/{self.normal_user.id}/demote/')
        self.assertEqual(demote.status_code, 200)
        self.assertFalse(demote.json().get('action_applied', False))
        self.normal_user.refresh_from_db()
        self.assertTrue(self.normal_user.is_staff)
        self.assertEqual(demote.json()['role'], 'ADMIN')

        second_demote = second_client.post(f'/api/admin/users/{self.normal_user.id}/demote/')
        self.assertEqual(second_demote.status_code, 200)
        self.assertTrue(second_demote.json().get('action_applied', False))
        self.normal_user.refresh_from_db()
        self.assertFalse(self.normal_user.is_staff)
        self.assertEqual(second_demote.json()['role'], 'USER')

    def test_admin_cannot_modify_own_role(self):
        client = Client()
        client.force_login(self.admin_user)

        promote = client.post(f'/api/admin/users/{self.admin_user.id}/promote/')
        demote = client.post(f'/api/admin/users/{self.admin_user.id}/demote/')

        self.assertEqual(promote.status_code, 403)
        self.assertEqual(demote.status_code, 403)

    def test_admin_user_list_hides_superusers(self):
        superuser = User.objects.create_user(username='rootadmin', email='rootadmin@example.com', password='StrongPass1!')
        superuser.is_superuser = True
        superuser.is_staff = True
        superuser.save(update_fields=['is_superuser', 'is_staff'])
        UserApproval.objects.create(user=superuser, is_approved=True)

        client = Client()
        client.force_login(self.admin_user)

        response = client.get('/api/admin/users/')

        self.assertEqual(response.status_code, 200)
        usernames = [user['username'] for user in response.json()]
        self.assertNotIn('rootadmin', usernames)
        self.assertIn(self.admin_user.username, usernames)
        self.assertIn(self.normal_user.username, usernames)

    def test_admin_can_approve_delete_and_reject_users(self):
        pending_user = User.objects.create_user(username='pendinguser', email='pending@example.com', password='StrongPass1!', is_active=False)
        UserApproval.objects.create(user=pending_user)

        approved_user = User.objects.create_user(username='approveduser', email='approved@example.com', password='StrongPass1!')
        UserApproval.objects.create(user=approved_user, is_approved=True)

        client = Client()
        client.force_login(self.admin_user)

        approve = client.post(f'/api/admin/users/{pending_user.id}/approve/')
        self.assertEqual(approve.status_code, 200)
        pending_user.refresh_from_db()
        self.assertTrue(UserApproval.objects.get(user=pending_user).is_approved)

        reject_target = User.objects.create_user(username='rejectme', email='reject@example.com', password='StrongPass1!', is_active=False)
        UserApproval.objects.create(user=reject_target)
        reject = client.post(f'/api/admin/users/{reject_target.id}/reject/')
        self.assertEqual(reject.status_code, 200)
        self.assertFalse(User.objects.filter(username='rejectme').exists())

        delete = client.delete(f'/api/admin/users/{approved_user.id}/')
        self.assertEqual(delete.status_code, 200)
        self.assertFalse(User.objects.filter(username='approveduser').exists())

    def test_approval_and_rejection_send_notification_emails(self):
        client = Client()
        client.force_login(self.admin_user)

        approved_user = User.objects.create_user(username='mailapprove', email='mailapprove@example.com', password='StrongPass1!', is_active=False)
        UserApproval.objects.create(user=approved_user)

        approve = client.post(f'/api/admin/users/{approved_user.id}/approve/')
        self.assertEqual(approve.status_code, 200)
        self.assertEqual(len(mail.outbox), 1)
        self.assertIn('approved', mail.outbox[0].subject.lower())
        self.assertIn('Approved by: roleadmin', mail.outbox[0].body)

        rejected_user = User.objects.create_user(username='mailreject', email='mailreject@example.com', password='StrongPass1!', is_active=False)
        UserApproval.objects.create(user=rejected_user)

        reject = client.post(
            f'/api/admin/users/{rejected_user.id}/reject/',
            data=json.dumps({'reason': 'Incomplete details'}),
            content_type='application/json',
        )
        self.assertEqual(reject.status_code, 200)
        self.assertFalse(User.objects.filter(username='mailreject').exists())
        self.assertEqual(len(mail.outbox), 2)
        self.assertIn('rejected', mail.outbox[-1].subject.lower())
        self.assertIn('Reason: Incomplete details', mail.outbox[-1].body)

    def test_role_changes_send_notification_emails_when_applied(self):
        client = Client()
        client.force_login(self.admin_user)

        first_vote = client.post(f'/api/admin/users/{self.normal_user.id}/promote/')
        self.assertEqual(first_vote.status_code, 200)
        self.assertEqual(len(mail.outbox), 0)

        second_client = Client()
        second_client.force_login(self.admin_user_2)
        second_vote = second_client.post(f'/api/admin/users/{self.normal_user.id}/promote/')
        self.assertEqual(second_vote.status_code, 200)
        self.assertTrue(second_vote.json().get('action_applied', False))
        self.assertEqual(len(mail.outbox), 1)
        self.assertIn('promoted', mail.outbox[0].subject.lower())
        self.assertIn('promoted to admin access', mail.outbox[0].body)

        first_demote = client.post(f'/api/admin/users/{self.normal_user.id}/demote/')
        self.assertEqual(first_demote.status_code, 200)
        self.assertEqual(len(mail.outbox), 1)

        second_demote = second_client.post(f'/api/admin/users/{self.normal_user.id}/demote/')
        self.assertEqual(second_demote.status_code, 200)
        self.assertTrue(second_demote.json().get('action_applied', False))
        self.assertEqual(len(mail.outbox), 2)
        self.assertIn('demoted', mail.outbox[-1].subject.lower())
        self.assertIn('demoted from admin access', mail.outbox[-1].body)


@override_settings(EMAIL_BACKEND='django.core.mail.backends.locmem.EmailBackend')
class EmailVerificationTests(TestCase):
    def setUp(self):
        self.client = Client()

    def _extract_uid_token(self, body):
        match = re.search(r'uid=([^&\s]+)&token=([^\s]+)', body)
        self.assertIsNotNone(match)
        return match.group(1), match.group(2)

    def test_register_creates_inactive_user_and_sends_verification_email(self):
        response = self.client.post(
            '/api/auth/register/',
            data=json.dumps({'username': 'verifyuser', 'first_name': 'Verify', 'last_name': 'User', 'email': 'verify@example.com', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )

        self.assertEqual(response.status_code, 201)
        user = User.objects.get(username='verifyuser')
        self.assertFalse(user.is_active)
        self.assertEqual(user.first_name, 'Verify')
        self.assertEqual(user.last_name, 'User')
        self.assertEqual(len(mail.outbox), 1)
        self.assertIn('verify-email?uid=', mail.outbox[0].body)

    def test_verify_endpoint_activates_user(self):
        register = self.client.post(
            '/api/auth/register/',
            data=json.dumps({'username': 'activateuser', 'email': 'activate@example.com', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )
        self.assertEqual(register.status_code, 201)

        uid, token = self._extract_uid_token(mail.outbox[-1].body)
        verify = self.client.post(
            '/api/auth/verify-email/',
            data=json.dumps({'uid': uid, 'token': token}),
            content_type='application/json',
        )
        self.assertEqual(verify.status_code, 200)

        user = User.objects.get(username='activateuser')
        self.assertTrue(user.is_active)

    def test_login_blocked_until_email_verified(self):
        register = self.client.post(
            '/api/auth/register/',
            data=json.dumps({'username': 'blockeduser', 'email': 'blocked@example.com', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )
        self.assertEqual(register.status_code, 201)

        blocked = self.client.post(
            '/api/auth/login/',
            data=json.dumps({'username': 'blockeduser', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )
        self.assertEqual(blocked.status_code, 403)

    def test_login_accepts_email_or_username(self):
        register = self.client.post(
            '/api/auth/register/',
            data=json.dumps({'username': 'emailuser', 'first_name': 'Email', 'last_name': 'Person', 'email': 'emailuser@example.com', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )
        self.assertEqual(register.status_code, 201)

        uid, token = self._extract_uid_token(mail.outbox[-1].body)
        verify = self.client.post(
            '/api/auth/verify-email/',
            data=json.dumps({'uid': uid, 'token': token}),
            content_type='application/json',
        )
        self.assertEqual(verify.status_code, 200)

        admin = User.objects.create_user(username='approver', email='approver@example.com', password='StrongPass1!')
        admin.is_staff = True
        admin.save(update_fields=['is_staff'])
        UserApproval.objects.create(user=admin, is_approved=True)
        admin_client = Client()
        admin_client.force_login(admin)
        approve = admin_client.post(f'/api/admin/users/{User.objects.get(username="emailuser").id}/approve/')
        self.assertEqual(approve.status_code, 200)

        by_username = self.client.post(
            '/api/auth/login/',
            data=json.dumps({'username': 'emailuser', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )
        self.assertEqual(by_username.status_code, 200)

        by_email = self.client.post(
            '/api/auth/login/',
            data=json.dumps({'username': 'emailuser@example.com', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )
        self.assertEqual(by_email.status_code, 200)

    def test_resend_verification_rate_limited(self):
        register = self.client.post(
            '/api/auth/register/',
            data=json.dumps({'username': 'resenduser', 'email': 'resend@example.com', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )
        self.assertEqual(register.status_code, 201)

        first = self.client.post(
            '/api/auth/resend-verification/',
            data=json.dumps({'email': 'resend@example.com'}),
            content_type='application/json',
        )
        second = self.client.post(
            '/api/auth/resend-verification/',
            data=json.dumps({'email': 'resend@example.com'}),
            content_type='application/json',
        )

        self.assertEqual(first.status_code, 200)
        self.assertEqual(second.status_code, 429)


@override_settings(EMAIL_BACKEND='django.core.mail.backends.locmem.EmailBackend')
class PasswordResetTests(TestCase):
    def setUp(self):
        self.client = Client()

    def _extract_uid_token(self, body):
        match = re.search(r'uid=([^&\s]+)&token=([^\s]+)', body)
        self.assertIsNotNone(match)
        return match.group(1), match.group(2)

    def test_password_reset_request_sends_email_for_active_user(self):
        User.objects.create_user(username='recoveruser', email='recover@example.com', password='StrongPass1!', is_active=True)

        response = self.client.post(
            '/api/auth/password-reset/request/',
            data=json.dumps({'email': 'recover@example.com'}),
            content_type='application/json',
        )

        self.assertEqual(response.status_code, 200)
        self.assertTrue(response.json()['account_found'])
        self.assertTrue(response.json()['email_sent'])
        self.assertEqual(len(mail.outbox), 1)
        self.assertIn('/reset-password?uid=', mail.outbox[0].body)

    def test_password_reset_request_reports_missing_account(self):
        response = self.client.post(
            '/api/auth/password-reset/request/',
            data=json.dumps({'email': 'missing@example.com'}),
            content_type='application/json',
        )

        self.assertEqual(response.status_code, 200)
        self.assertFalse(response.json()['account_found'])
        self.assertFalse(response.json()['email_sent'])
        self.assertEqual(len(mail.outbox), 0)

    def test_password_reset_confirm_updates_password(self):
        user = User.objects.create_user(username='recoverconfirm', email='recoverconfirm@example.com', password='StrongPass1!', is_active=True)
        # mark programmatically-created active test accounts as admin-approved for legacy tests
        UserApproval.objects.create(user=user, is_approved=True)

        request_response = self.client.post(
            '/api/auth/password-reset/request/',
            data=json.dumps({'email': 'recoverconfirm@example.com'}),
            content_type='application/json',
        )
        self.assertEqual(request_response.status_code, 200)

        uid, token = self._extract_uid_token(mail.outbox[-1].body)
        confirm_response = self.client.post(
            '/api/auth/password-reset/confirm/',
            data=json.dumps({'uid': uid, 'token': token, 'new_password': 'NewStrongPass1!', 'confirm_password': 'NewStrongPass1!'}),
            content_type='application/json',
        )

        self.assertEqual(confirm_response.status_code, 200)

        user = User.objects.get(username='recoverconfirm')
        self.assertTrue(user.check_password('NewStrongPass1!'))
        self.assertFalse(user.check_password('StrongPass1!'))

        old_login = self.client.post(
            '/api/auth/login/',
            data=json.dumps({'username': 'recoverconfirm', 'password': 'StrongPass1!'}),
            content_type='application/json',
        )
        new_login = self.client.post(
            '/api/auth/login/',
            data=json.dumps({'username': 'recoverconfirm', 'password': 'NewStrongPass1!'}),
            content_type='application/json',
        )

        self.assertEqual(old_login.status_code, 401)
        self.assertEqual(new_login.status_code, 200)
