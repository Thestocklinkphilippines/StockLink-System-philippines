# Quick Classification: Critical vs. Debug Events

## 🚨 Port 2325 - CRITICAL EVENTS
Use `LOG_SYSTEM_ALERT()` or `LOG_SYSTEM_EVENT()`

### Feeding & Water
- ✅ "Feed dispensed: 0.500kg"
- ✅ "Water refill complete"
- ✅ "Low feed warning!"
- ✅ "Low water warning!"

### Power & Battery
- ✅ "Mains power lost"
- ✅ "Mains power restored"
- ✅ "Battery critical shutdown"

### Network
- ✅ "WiFi connected IP=192.168.0.102"
- ✅ "WiFi disconnected - offline mode"
- ✅ "Cloud sync succeeded"
- ✅ "Cloud sync failed - buffering"

### Errors
- ✅ "Motor malfunction detected!"
- ✅ "Sensor failure - ADC error"

### Device State
- ✅ "Device boot complete"
- ✅ "Schedule executed"
- ✅ "Configuration updated"

---

## 🔧 Port 2324 - DEBUG EVENTS
Use `Serial.print()`, `LOG_INFO()`, `LOG_DEBUG()`, etc.

### Sensor Readings
- ❌ "Feeder ADC: 2048"
- ❌ "Water voltage: 1.23V"

### Routine Operations
- ❌ "Polling sensors..."
- ❌ "Main loop iteration #1234"
- ❌ "Waiting for NTP..."

### Verbose Transitions
- ❌ "WiFi status: WL_CONNECTING"
- ❌ "Attempting reconnection..."

### Configuration Loading
- ❌ "Configuration loaded from Preferences"
- ❌ "JSON parsed successfully"

---

## Quick Rule
```
If someone SLEEPING should WAKE UP to handle it
→ Use Port 2325 (Critical)

If it's just informational/debugging
→ Use Port 2324 (Debug)
```

---

## Current System Critical Events

```
LOW_FEED             Insufficient feed for operation
LOW_WATER            Insufficient water level
POWER_OUTAGE         Mains power lost
POWER_RESTORED       Mains power reconnected
FEEDING              Feed dispense executed
WATERING             Water refill executed
BATTERY_SHUTDOWN     Low battery emergency stop
WIFI_CONNECTED       Network online
WIFI_DISCONNECTED    Network offline
SCHEDULE_EXECUTED    Schedule ran (feeding/watering)
CONFIG_UPDATED       Settings changed
SYSTEM_READY         Device boot complete
```

---

## Implementation

```cpp
// Critical event (Port 2325)
LOG_SYSTEM_ALERT("Low feed detected!");
LOG_SYSTEM_EVENT("Feed dispensed: 0.500kg");

// Debug message (Port 2324)
LOG_DEBUG("Sensor reading: ADC=%d", value);
LOG_INFO("Configuration loaded successfully");
```

See **CRITICAL_EVENTS_GUIDE.md** for complete classification and examples.
