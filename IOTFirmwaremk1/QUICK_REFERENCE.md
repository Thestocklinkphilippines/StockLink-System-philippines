# Quick Reference: Multi-Port Console

## 🚀 Connecting to ESP32 Ports

```bash
# Command Console (send commands, get responses)
nc <ESP32_IP> 2323

# Debug Console (watch all logs)
nc <ESP32_IP> 2324

# System Console (critical alerts only)
nc <ESP32_IP> 2325
```

## 📡 Port Types & Content

| Port | Type | Content | Example |
|------|------|---------|---------|
| **2323** | Bi-directional | `help`, `dump_state`, `sched_list` | Commands and responses |
| **2324** | Output only | `[INFO]`, `[DBG ]`, `[WARN]`, `[ERR ]` | All debug output |
| **2325** | Output only | `[ALERT]`, `[EVENT]` | Critical events only |

## 💻 Monitoring Setup

### Simple (All in One)
```bash
# All three terminals, one line each
nc 192.168.0.102 2323 &
nc 192.168.0.102 2324 &
nc 192.168.0.102 2325 &
```

### Focused (Alerts Only)
```bash
nc 192.168.0.102 2325  # Watch critical events
```

### Development (Logs + Commands)
```bash
# Terminal 1: Debug logs
nc 192.168.0.102 2324

# Terminal 2: Command entry and responses
nc 192.168.0.102 2323
```

## 🛠️ Available Commands

Type `help` on **port 2323** for full list:

```
help              Show command help
dump_cfg          Print JSON configuration
dump_state        Print runtime state
dump_prefs        Print preferences
sched_list        List feeding schedules
sched_run [id]    Run schedule now
sim_set [type]    Simulation mode
keypad_input      Enable/disable keypad
keypad_cal        Calibrate keypad
```

## 📊 Port Routing (For Developers)

### Standard Serial (Default → Debug Port 2324)
```cpp
Serial.print("message");        // → Port 2324
Serial.printf("value: %d", x);  // → Port 2324
```

### Debug Logging (All → Debug Port 2324)
```cpp
LOG_DEBUG("msg");    // → Port 2324 (if verbose mode)
LOG_INFO("msg");     // → Port 2324
LOG_WARN("msg");     // → Port 2324
LOG_ERROR("msg");    // → Port 2324
```

### System Alerts (→ System Port 2325)
```cpp
LOG_SYSTEM_ALERT("critical!");   // → Port 2325
LOG_SYSTEM_EVENT("feeder run");  // → Port 2325
```

### Direct Port-Specific (New APIs)
```cpp
SFMultiConsole.debugPrintf("Debug: %s", msg);    // → Port 2324
SFMultiConsole.systemPrintf("Alert: %s", msg);   // → Port 2325
SFMultiConsole.commandPrintf("Response: %s", x); // → Port 2323
```

## ⚡ Performance Notes

- **RAM Used**: +3 KB for 3 ports vs 1 port
- **Clients Per Port**: 2 (configurable)
- **Buffer Size**: 512 bytes for printf
- **Latency**: <1ms with TCP NoDelay enabled
- **CPU Impact**: Negligible (event-driven)

## 🔌 Connection Tips

### Using `telnet`
```bash
telnet 192.168.0.102 2323
# Press Ctrl-] then type 'quit' to exit
```

### Using Python
```python
import socket
s = socket.socket()
s.connect(('192.168.0.102', 2324))
print(s.recv(1024).decode())
```

### Using VS Code
Install extension: `tcp-client`
Then: Command Palette → "TCP Client: Connect" → `192.168.0.102:2324`

## ❌ Troubleshooting

**Can't connect?**
- Check ESP32 IP address (use `sched_list` on working port first)
- Verify WiFi is connected (should see [INFO] messages on port 2324)
- Try 192.168.0.102 or `smart-feeder-001.local` (if mDNS enabled)

**No output on port 2324?**
- Verify WiFi connection
- Check if in calibration mode (port 2323 will show calibration prompts)
- Try port 2325 first to confirm network works

**Commands not working?**
- Ensure you're on **port 2323** (command port)
- Type `help` first to see available commands
- Command responses appear on same port (2323)

## 🎯 Common Tasks

### Monitor Feeding Events
```bash
nc 192.168.0.102 2325  # Watch for feeding events
```

### Debug WiFi Issues
```bash
# Terminal 1
nc 192.168.0.102 2324

# Terminal 2
nc 192.168.0.102 2323
> dump_state  # Check WiFi status
```

### Calibrate Keypad
```bash
nc 192.168.0.102 2323
> keypad_cal
# Follow prompts
```

### Run Manual Feed Test
```bash
nc 192.168.0.102 2323
> sched_list     # See available schedules
> sched_run 1    # Run schedule ID 1
```

## 🔄 Architecture

```
Internet/Local Network
         ↓
    WiFi Module
         ↓
    ┌────┴────┬────────┬──────────┐
    ↓         ↓        ↓          ↓
  Port      Port    Port        USB
  2323      2324    2325       Serial
  CMDS      DEBUG   ALERTS    (Mirror)
  (RW)      (W)     (W)       (RW)
```

## 💡 Design Benefits

1. **Clean Separation**: No mixing of concerns
2. **Easy Filtering**: Watch only what matters
3. **Scalability**: Multiple clients per port
4. **Low Overhead**: Only 3 KB extra memory
5. **High Performance**: Event-driven, non-blocking
6. **Developer Friendly**: Simple port-based organization

---

**ESP32 IP**: Usually `192.168.0.102` (check your WiFi settings)

**Default Ports**: Command=2323, Debug=2324, System=2325

**Setup Time**: ~3 seconds from power-on

**Max Clients**: 2 per port (configurable)
