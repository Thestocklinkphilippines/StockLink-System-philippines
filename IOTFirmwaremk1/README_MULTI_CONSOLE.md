# ESP32 Multi-Port Wireless Console Implementation

## 🎉 What's Changed?

The ESP32 firmware has been radically redesigned to use **3 dedicated TCP ports** instead of a single port for all serial events and commands.

### Before: Single Port Chaos
```
Port 2323
├─ Your commands
├─ Debug logs
├─ System alerts  
├─ Sensor data
├─ Network info
└─ Everything mixed together!
```

### After: Clean Separation
```
Port 2323 (Commands)     → Send commands, get responses
Port 2324 (Debug)        → All logs and debug info
Port 2325 (System/Alert) → Critical events only
```

## 🚀 Quick Start: How to Use It

### Option 1: Monitor Only System Alerts
```bash
nc 192.168.0.102 2325
```
You'll see only critical events like feeding, watering, power issues.

### Option 2: Monitor Only Debug Logs
```bash
nc 192.168.0.102 2324
```
You'll see all the verbose system logging and debug information.

### Option 3: Send Commands
```bash
nc 192.168.0.102 2323
```
Type `help` to see available commands, then send commands and get responses.

### Option 4: Monitor Everything (3 Terminals)
```bash
# Terminal 1
nc 192.168.0.102 2323    # Commands

# Terminal 2
nc 192.168.0.102 2324    # Debug logs

# Terminal 3
nc 192.168.0.102 2325    # System alerts
```

## 📋 Available Commands (on Port 2323)

```
help              Show this help
dump_cfg          Print full JSON configuration
dump_state        Print runtime state snapshot
dump_prefs        Print preferences
sched_list        List all feeding schedules
sched_run [id]    Run a schedule now
sim_set           Set simulation values (debug mode)
keypad_cal        Calibrate the keypad
dump_outbox       Show buffered events
```

## 🔌 Port Details

### Port 2323: Command Console
- **Type**: Bi-directional (send & receive)
- **Content**: Commands you send, responses you get
- **Max Clients**: 2 simultaneous
- **Responses**: All command output appears here
- **Example**:
  ```
  > help
  ================ SMART FEEDER SERIAL COMMANDS ================
  help         -> show this command list
  dump_cfg     -> print full local JSON config from Preferences
  ...
  ```

### Port 2324: Debug Console
- **Type**: Output only (receive only)
- **Content**: All `Serial.print()` and logging output
- **Max Clients**: 2 simultaneous
- **Includes**: `[INFO]`, `[DEBUG]`, `[WARN]`, `[ERROR]` messages
- **Example**:
  ```
  [INFO] Smart feeder booting
  [INFO] Build mode simulation=1 verbose=0
  [INFO] I2C initialized SDA=21 SCL=22
  [DEBUG] NTP wait 1 current=1000
  [INFO] Setup complete mainsPresent=1
  ```

### Port 2325: System/Alert Console
- **Type**: Output only (receive only)
- **Content**: Critical system events and alerts ONLY
- **Max Clients**: 2 simultaneous
- **Includes**: `[ALERT]`, `[EVENT]` messages
- **Example**:
  ```
  [EVENT] Feeder run initiated at 14:30:00
  [ALERT] Feed dispensed: 250g
  [ALERT] Low water detected!
  [EVENT] Water refill complete
  ```

## 💡 Use Cases

### Scenario 1: Troubleshoot Feeding Issue
1. Open two terminals:
   ```bash
   # Terminal 1: Watch what happens
   nc 192.168.0.102 2324

   # Terminal 2: Send test command
   nc 192.168.0.102 2323
   ```
2. Send command: `sched_run 1`
3. Watch Terminal 1 for debug logs showing exactly what happened

### Scenario 2: Monitor Overnight
```bash
# Just watch system alerts, ignore debug noise
nc 192.168.0.102 2325
```
You'll see feeding events, water level changes, power issues - everything important.

### Scenario 3: Debug WiFi Connectivity
1. Connect to debug port:
   ```bash
   nc 192.168.0.102 2324
   ```
2. Watch for WiFi reconnection logs
3. Use command port to check status:
   ```bash
   nc 192.168.0.102 2323
   > dump_state
   ```

### Scenario 4: Multi-User Access
- User 1 monitors alerts: `nc 192.168.0.102 2325`
- User 2 monitors logs: `nc 192.168.0.102 2324`
- User 3 sends commands: `nc 192.168.0.102 2323`
- All work simultaneously with no interference!

## 🎯 Key Benefits

| Aspect | Before | After |
|--------|--------|-------|
| **Port Count** | 1 | 3 |
| **Command Entry** | Mixed with logs | Dedicated port |
| **Alert Visibility** | Buried in debug | Dedicated port |
| **Multiple Monitors** | Chaotic | Clean |
| **Memory Overhead** | N/A | +3 KB (acceptable) |
| **Ease of Use** | Hard | Very Easy |

## 🔧 For Developers

### Using Port-Specific Logging

In your C++ code:

```cpp
// Goes to debug port (2324)
Serial.print("Debug message");
LOG_INFO("Information");
LOG_DEBUG("Debug details");

// Goes to system/alert port (2325)
LOG_SYSTEM_ALERT("Critical alert!");
LOG_SYSTEM_EVENT("Important event");

// Goes to command port (2323)
SFMultiConsole.commandPrintf("Command response: %s", resp);
```

### Direct Port Access

```cpp
// Write directly to ports
SFMultiConsole.writeDebug((uint8_t*)"data", 4);     // Port 2324
SFMultiConsole.writeSystem((uint8_t*)"data", 4);    // Port 2325
SFMultiConsole.writeCommand((uint8_t*)"data", 4);   // Port 2323
```

## 📁 Files Added/Modified

### New Files
- `include/sf_multi_console.h` - Multi-port console class
- `src/sf_multi_console.cpp` - Implementation
- `MULTI_CONSOLE_GUIDE.md` - Detailed usage guide (this directory)
- `QUICK_REFERENCE.md` - Quick reference card (this directory)

### Modified Files
- `src/sf_wireless_console.cpp` - Now delegates to multi-console
- `src/main.cpp` - Initializes multi-console
- `include/sf_debug.h` - Added system alert macros

### Unchanged (Backward Compatible)
- All existing Serial.print() code works unchanged
- Commands still work the same way
- USB serial still works

## 🏗️ Architecture

```cpp
// The old single-port design:
class WirelessConsole : public Print {
  WiFiServer server_(2323);   // Single server for everything
};

// The new multi-port design:
class MultiWirelessConsole : public Print {
  WiFiServer commandServer_(2323);   // Commands only
  WiFiServer debugServer_(2324);     // Debug only
  WiFiServer systemServer_(2325);    // System alerts only
};
```

## 📊 Memory Impact

- **Old Design**: ~15 KB for single WiFiServer + client
- **New Design**: ~18 KB for 3 servers (3x WiFiServer instances)
- **Overhead**: Only ~3 KB additional
- **Acceptable**: ESP32 has 320 KB SRAM

## ✅ Backward Compatibility

✓ Existing `Serial.print()` calls work unchanged
✓ All commands work the same
✓ USB serial still mirrors all output
✓ Old `WirelessConsole` class still exists (delegates to new system)
✓ No code breaking changes

## 🐛 Troubleshooting

### "Can't connect to any port"
1. Verify ESP32 IP: Usually `192.168.0.102`
2. Check WiFi is connected: Try `nc 192.168.0.102 2324` first
3. Try alternate hostname: `nc smart-feeder-001.local 2324`

### "Port 2324 has no output"
1. Ensure WiFi is connected (check port 2324 first)
2. Might be in calibration mode - try port 2323
3. Check USB serial output as fallback

### "Commands not working"
1. Ensure you're on **Port 2323** (command port)
2. Type `help` to confirm port is working
3. Check port 2324 for error messages

### "System alerts missing"
1. Might need to trigger an event (feed, water, etc.)
2. Check that device code uses `LOG_SYSTEM_ALERT()` for your event
3. Verify connection to port 2325

## 📖 Documentation

1. **This file**: Overview and quick start
2. **MULTI_CONSOLE_GUIDE.md**: Detailed usage guide
3. **QUICK_REFERENCE.md**: Quick reference card
4. **Code comments**: See sf_multi_console.h and sf_multi_console.cpp

## 🎓 Learning Path

1. **Start**: Read this file (you're here!)
2. **Try**: Connect to port 2324: `nc 192.168.0.102 2324`
3. **Explore**: Connect to port 2323 and type `help`
4. **Monitor**: Connect to port 2325 and trigger a feeding event
5. **Master**: Read MULTI_CONSOLE_GUIDE.md for advanced usage

## 🚀 Next Steps

1. **Build**: `pio run` (in project directory)
2. **Upload**: `pio run -t upload` or use OTA
3. **Test**: Open 3 terminals as shown above
4. **Celebrate**: Enjoy cleaner console experience! 🎉

---

**Questions?** Check MULTI_CONSOLE_GUIDE.md or QUICK_REFERENCE.md
**Found a bug?** Check the debug port (2324) for error messages
**Want to modify?** See "For Developers" section in MULTI_CONSOLE_GUIDE.md
