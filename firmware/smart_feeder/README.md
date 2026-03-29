# ESP32 Firmware — Smart Feeder (Arduino)

What this contains

- `smart_feeder.ino`: Example Arduino (ESP32) firmware that implements:
  - Wi‑Fi connection and NTP time sync
  - Persistent configuration using `Preferences` (authoritative on device)
  - Polling-based config sync with Django server (GET/POST) using token auth
  - Last-Write-Wins (LWW) conflict resolution using UTC timestamps
  - Basic feeding schedule execution and feed-level checks
  - Auto rewatering stub with alerting
  - Sending logs and alerts to server endpoints

Dependencies

- ESP32 Arduino core
- ArduinoJson library

How to use

1. Open `smart_feeder.ino` in Arduino IDE or PlatformIO.
2. Fill in Wi‑Fi credentials and `DEVICE_ID` / `AUTH_TOKEN` at the top of the sketch.
3. Install ArduinoJson via the Library Manager.
4. Build and flash to your ESP32.

Notes

- The sketch uses simple placeholders for sensors/actuators. Replace `readFeedLevel()`,
  `dispenseFeed(amountKg)`, and `readWaterLevel()` with your hardware code.
- The server base URL is configurable; set to your Django server address.
