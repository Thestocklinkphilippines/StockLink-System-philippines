# 🚨 Critical Events Classification for ESP32 Smart Feeder

## What Are "Critical" Events?

Critical events are **system-level occurrences** that require immediate attention or are important for operational oversight. They are routed to **Port 2325 (System/Alert Console)** using:

```cpp
LOG_SYSTEM_ALERT("alert message");   // For critical issues
LOG_SYSTEM_EVENT("event message");   // For important system events
```

---

## 📊 Critical Event Categories

### 1. **Feeding Events** ✅ CRITICAL
Events related to the feeding motor and feeder operation.

**Examples:**
- Schedule-based feeding initiated
- Manual feed command executed (`sched_run`)
- Feeding completed successfully
- Motor start/stop
- Feed dispensed (with amount)

**When to log:**
```cpp
LOG_SYSTEM_EVENT("Feeding started: schedule_id=101 amount=0.500kg");
LOG_SYSTEM_EVENT("Feed dispensed: 0.500kg remaining=1.957kg");
LOG_SYSTEM_EVENT("Feeding completed successfully");
```

---

### 2. **Watering Events** ✅ CRITICAL
Events related to water refill and pump operation.

**Examples:**
- Water refill initiated
- Water refill completed
- Pump activated/deactivated
- Water level changes (critical thresholds)

**When to log:**
```cpp
LOG_SYSTEM_EVENT("Water refill pump activated");
LOG_SYSTEM_EVENT("Water refill in progress (15% -> 95%)");
LOG_SYSTEM_EVENT("Water refill pump deactivated");
LOG_SYSTEM_EVENT("Water level: 95% (nominal)");
```

---

### 3. **Low Feed Alert** ⚠️ CRITICAL
When feed supply falls below minimum operational threshold.

**Trigger:**
- Feed remaining < 0.5 kg (configurable threshold)
- Schedule cannot be run due to insufficient feed

**When to log:**
```cpp
LOG_SYSTEM_ALERT("Low feed detected! Remaining: 0.250kg");
LOG_SYSTEM_ALERT("Schedule blocked: insufficient feed (required=0.500kg, available=0.250kg)");
```

---

### 4. **Low Water Alert** ⚠️ CRITICAL
When water level falls below operational minimum.

**Trigger:**
- Water sensor reads below 20% threshold
- Refill cannot complete

**When to log:**
```cpp
LOG_SYSTEM_ALERT("Low water detected! (below 20%)");
LOG_SYSTEM_ALERT("Water refill failed - pump malfunction");
```

---

### 5. **Power Events** 🔌 CRITICAL
Mains power state changes affecting device operation.

**Examples:**
- Power outage detected
- Power restored
- Battery low warning
- Battery critical shutdown

**When to log:**
```cpp
LOG_SYSTEM_ALERT("Mains power lost!");
LOG_SYSTEM_EVENT("Mains power restored");
LOG_SYSTEM_ALERT("Low battery detected: 8.9V (threshold 9.0V)");
LOG_SYSTEM_ALERT("Battery critical - device shutdown imminent");
```

---

### 6. **Network Events** 📡 CRITICAL
WiFi connectivity and cloud sync status.

**Examples:**
- WiFi connected/disconnected
- Cloud sync succeeded/failed
- Offline event queue status
- Configuration updated from cloud

**When to log:**
```cpp
LOG_SYSTEM_EVENT("WiFi connected IP=192.168.0.102");
LOG_SYSTEM_ALERT("WiFi disconnected - services paused");
LOG_SYSTEM_EVENT("Cloud sync succeeded: 5 events sent");
LOG_SYSTEM_ALERT("Cloud sync failed - buffering offline");
```

---

### 7. **Device Shutdown Events** 🛑 CRITICAL
Controlled shutdown and boot sequences.

**Examples:**
- Device boot initiated
- Device entering low-battery shutdown
- Emergency shutdown activated
- System reset

**When to log:**
```cpp
LOG_SYSTEM_EVENT("Device boot complete");
LOG_SYSTEM_ALERT("Low battery shutdown relay asserted at 8.9V");
LOG_SYSTEM_ALERT("Emergency shutdown initiated");
LOG_SYSTEM_EVENT("System ready for operation");
```

---

### 8. **Configuration Changes** ⚙️ SEMI-CRITICAL
Important configuration updates that affect operation.

**Examples:**
- Schedule added/removed/modified
- Max feed capacity updated
- Calibration parameters changed
- Keypad recalibrated

**When to log:**
```cpp
LOG_SYSTEM_EVENT("Schedule created: id=105 time=14:30 amount=0.500kg");
LOG_SYSTEM_EVENT("Keypad calibration complete: idle_is_low=true");
LOG_SYSTEM_EVENT("Max feed capacity updated: 5.000kg");
```

---

### 9. **Error & Failure Events** ❌ CRITICAL
System errors and operational failures.

**Examples:**
- Motor malfunction
- Sensor failure
- Configuration error
- Hardware error

**When to log:**
```cpp
LOG_SYSTEM_ALERT("Motor malfunction detected!");
LOG_SYSTEM_ALERT("Feed sensor reading invalid: ADC=-1");
LOG_SYSTEM_ALERT("Configuration load failed - using defaults");
```

---

## 📋 Not Critical (Use Debug Port 2324 Instead)

### Debug/Info Messages (NOT for port 2325):
- ADC readings: `[DBG ] Sensor reading: feeder=85.2% water=92.5%`
- Loop iterations: `[DBG ] Main loop iteration #1234`
- Timing info: `[DBG ] NTP wait 1 current=1000`
- Verbose transitions: `[DBG ] WiFi status changed to: WL_CONNECTING`

**These go to Port 2324:**
```cpp
LOG_DEBUG("ADC feeder reading: %d", adcValue);
LOG_INFO("Configuration loaded from Preferences");
```

---

## 🎯 Decision Tree: Is It Critical?

```
Does the event affect device operation?
├─ YES: Is it time-sensitive or requires immediate action?
│   ├─ YES → CRITICAL (use LOG_SYSTEM_ALERT or LOG_SYSTEM_EVENT)
│   └─ NO → Check categories below
│
├─ NO: Is it a state change users should know about?
│   ├─ YES (feeding, water, power, network) → CRITICAL
│   └─ NO → Use debug logging (port 2324)
│
└─ RULE OF THUMB:
    If a user sleeping should wake up to handle it
    → It's CRITICAL (port 2325)
    
    If it's informational but not urgent
    → It's DEBUG (port 2324)
```

---

## 📌 Implementation Examples

### Example 1: Feeding Event (CRITICAL)
```cpp
// In sf_actuators.cpp when dispensing feed
if (dispenseFeed(amt, cfg)) {
  LOG_SYSTEM_EVENT("Feed dispensed: %.3fkg remaining=%.3fkg", amt, remaining);
  sendLog("feeding", payload.as<JsonVariant>());
} else {
  LOG_SYSTEM_ALERT("Feed dispensing failed!");
}
```

### Example 2: Low Feed Alert (CRITICAL)
```cpp
// In sf_scheduler.cpp when checking sufficiency
if (!isFeedSufficient(requiredKg, cfg)) {
  LOG_SYSTEM_ALERT("Low feed: required=%.3fkg available=%.3fkg", 
                   requiredKg, availableKg);
  sendAlert("low_feed");
}
```

### Example 3: Power Event (CRITICAL)
```cpp
// In sf_sensors.cpp when power status changes
if (powerRestored) {
  LOG_SYSTEM_EVENT("Mains power restored");
  sendAlert("power_restored");
}
```

### Example 4: Debug Message (NOT CRITICAL)
```cpp
// In sensor reading loop - goes to port 2324
LOG_DEBUG("Feeder ADC: %d Voltage: %.2fV", adc, voltage);
LOG_INFO("Configuration reloaded from Preferences");
```

---

## 🔍 Current System Critical Events

Based on code analysis, here are the events the system currently tracks as "important":

```cpp
// From sf_network.cpp - isQueuedLogImportant()
✓ "feeding"              - Feed dispense events
✓ "watering"             - Water refill events
✓ "power"                - Mains power state changes
✓ "feed_now"             - Manual feed command
✓ "low_feed"             - Low feed alert
✓ "low_water"            - Low water alert
✓ "shutdown"             - System shutdown events
```

---

## 📢 Alert Types Used by sendAlert()

The system already uses these alert types (which should be logged as CRITICAL):

```
"low_feed"               - Feed supply low
"low_water"              - Water level low
"power_outage"           - Mains power lost
"power_restored"         - Mains power restored
"low_battery_shutdown"   - Device shutting down (low battery)
"feeding"                - Feed scheduled/executed
"watering"               - Water refill occurred
```

---

## 💡 Best Practices

### ✅ DO Log as Critical (Port 2325):
- User-visible status changes
- Alerts requiring attention
- Important operational events
- Power/battery status
- Feed/water level changes
- WiFi connectivity
- Configuration updates

### ❌ DON'T Log as Critical:
- Loop iterations
- ADC readings (unless error)
- NTP sync attempts
- Function entry/exit
- Verbose debug traces
- Routine sensor polling

### Example: Good Critical Logging
```cpp
// ✅ GOOD - User cares
LOG_SYSTEM_EVENT("Feeding: Schedule executed");
LOG_SYSTEM_ALERT("Warning: Low feed remaining (250g)");
LOG_SYSTEM_EVENT("WiFi connected: 192.168.0.102");

// ❌ TOO VERBOSE - Every loop tick
LOG_SYSTEM_EVENT("Loop iteration 12345");
LOG_SYSTEM_EVENT("Polling sensors...");
```

---

## 🎯 Summary Table

| Event Type | Severity | Example | Port |
|---|---|---|---|
| Feeding | HIGH | "Feed dispensed 0.5kg" | 2325 |
| Watering | HIGH | "Water refill complete" | 2325 |
| Low Feed | HIGH | "Low feed warning" | 2325 |
| Low Water | HIGH | "Water below minimum" | 2325 |
| Power | HIGH | "Mains power lost" | 2325 |
| WiFi | MEDIUM | "WiFi connected" | 2325 |
| Config | MEDIUM | "Schedule updated" | 2325 |
| Debug | LOW | "ADC reading: 2048" | 2324 |
| Info | LOW | "Sensor poll complete" | 2324 |

---

**Key Principle**: If it affects users or requires attention → Port 2325 (Critical)
If it's informational → Port 2324 (Debug)
