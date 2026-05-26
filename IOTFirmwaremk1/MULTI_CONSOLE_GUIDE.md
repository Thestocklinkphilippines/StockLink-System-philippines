# Multi-Port WiFi Console Usage Guide

## Overview

The ESP32 firmware now exposes **3 dedicated TCP ports** instead of a single port. Each port serves a specific purpose, making it easier to monitor and interact with different types of events.

## Port Allocations

### Port 2323 - Command Console (Bi-directional)
**Purpose**: Send commands to the device and receive command responses

**Usage**:
```bash
# Connect using netcat
nc <esp32-ip> 2323

# Or telnet
telnet <esp32-ip> 2323

# Or with nc on Windows
ncat <esp32-ip> 2323
```

**What appears here**:
- Command prompts and banners
- Command responses (help, status, errors)
- Command execution output (dump_cfg, dump_state, etc.)

**Example**:
```
$ nc 192.168.0.102 2323
=== ESP32 Command Console (Port 2323) ===
Send commands here. Responses appear on this port.
Type 'help' for available commands.

> help
================ SMART FEEDER SERIAL COMMANDS ================
help         -> show this command list
dump_cfg     -> print full local JSON config from Preferences
dump_state   -> print runtime state snapshot
...
```

### Port 2324 - Debug Console (Output only)
**Purpose**: View all debug logs and general system output

**Usage**:
```bash
# Connect using netcat
nc <esp32-ip> 2324

# Or telnet
telnet <esp32-ip> 2324
```

**What appears here**:
- All `Serial.print()` and `Serial.println()` output
- `LOG_DEBUG()`, `LOG_INFO()`, `LOG_WARN()`, `LOG_ERROR()` messages
- General informational messages about system state
- Debug information from various subsystems

**Example**:
```
$ nc 192.168.0.102 2324
=== ESP32 Debug Console (Port 2324) ===
All debug logs and general output appear here.

[INFO] Smart feeder booting
[INFO] Build mode simulation=1 verbose=0
[INFO] Pins initialized
[INFO] I2C initialized SDA=21 SCL=22
[DBG ] NTP wait 1 current=1000
[INFO] LCD initialized addr=0x27 cols=20 rows=4
[INFO] Setup complete mainsPresent=1
```

### Port 2325 - System/Alert Console (Output only)
**Purpose**: Receive only critical system events and alerts

**Usage**:
```bash
# Connect using netcat
nc <esp32-ip> 2325

# Or telnet
telnet <esp32-ip> 2325
```

**What appears here**:
- Critical alerts and events (via `LOG_SYSTEM_ALERT()`)
- System state changes (via `LOG_SYSTEM_EVENT()`)
- Feeding events, watering events, power events
- Low battery warnings
- Network connectivity issues

**Example**:
```
$ nc 192.168.0.102 2325
=== ESP32 System Console (Port 2325) ===
Critical system events and alerts appear here.

[ALERT] Feeder run initiated at 14:30:00
[EVENT] Feed dispensed: 250g
[ALERT] Low water detected!
[EVENT] Water refill complete
```

## Monitoring Scenarios

### Scenario 1: Monitor Everything
Open 3 terminals:
```bash
# Terminal 1: Debug logs
nc 192.168.0.102 2324

# Terminal 2: System alerts
nc 192.168.0.102 2325

# Terminal 3: Send commands (if needed)
nc 192.168.0.102 2323
```

### Scenario 2: Monitor Only Critical Alerts
```bash
# One terminal for system events
nc 192.168.0.102 2325
```

### Scenario 3: Send Commands + View Responses
```bash
# One terminal for commands and their responses
nc 192.168.0.102 2323
> help
> dump_state
> sched_list
```

### Scenario 4: Debug a Specific Issue
```bash
# Terminal 1: Only debug output
nc 192.168.0.102 2324

# Terminal 2: Only system alerts
nc 192.168.0.102 2325
```

## Available Commands

Type `help` on the **Command Console (Port 2323)** to see all commands:

```
help         -> show this command list
dump_cfg     -> print full local JSON config from Preferences
dump_state   -> print runtime state snapshot
dump_prefs   -> print key preferences values
sched_list   -> list schedules from local config
sched_run    -> run schedule now (ignore time): sched_run [id]
sim_set      -> set sim value: sim_set feeder|water|mains <value>
sim_defaults -> reset sim values to defaults
keypad_input -> toggle keypad polling: keypad_input [toggle|on|off|status]
keypad_cal   -> interactive keypad ADC calibration wizard
dump_outbox  -> print buffered offline event queue
```

## Key Differences from Single-Port Design

### Before (Single Port 2323)
- All logs mixed with commands
- Hard to filter critical alerts from debug noise
- All output on one connection
- Multiple clients compete for same stream

### After (Three Dedicated Ports)
- **Clean separation**: Commands separate from logs
- **Easy filtering**: Critical alerts in dedicated port
- **Better monitoring**: Can watch only what you need
- **Scalability**: Multiple clients can connect to same port
- **Reduced noise**: Debug info doesn't clutter alert stream

## Backward Compatibility

The original `WirelessConsole` class is still available and works, but it now **delegates to the new multi-console internally**. This means:

1. Existing code using `Serial.print()` still works
2. Default behavior routes to the debug port (2324)
3. New code can use port-specific logging:
   ```cpp
   SFMultiConsole.debugPrintf("Debug message");
   SFMultiConsole.systemPrintf("Alert message");
   SFMultiConsole.commandPrintf("Command response");
   ```

## Implementation Details

### Files Added
- `include/sf_multi_console.h` - Multi-port console interface
- `src/sf_multi_console.cpp` - Multi-port implementation

### Files Modified
- `src/sf_wireless_console.cpp` - Now delegates to multi-console
- `src/main.cpp` - Uses new multi-console
- `include/sf_debug.h` - Added `LOG_SYSTEM_ALERT()` and `LOG_SYSTEM_EVENT()` macros

### Port Server Details
- **Max clients per port**: 2 (configurable)
- **Buffer size for printf**: 512 bytes
- **TCP NoDelay**: Enabled for all ports (low latency)
- **Server startup**: Automatic on WiFi connect

## Memory Usage

The multi-port design uses approximately **3 KB additional heap memory** compared to the single-port design:
- 3 WiFiServer instances: ~1.5 KB
- 6 WiFiClient objects (2 per port): ~1.5 KB
- Total overhead: ~3 KB

This is acceptable for an ESP32 with 320 KB of SRAM.

## Troubleshooting

### Cannot connect to any port
1. Verify ESP32 is connected to WiFi: `nc <esp32-ip> 2324` and look for WiFi status
2. Check IP address: Should match the device's actual network IP
3. Ensure firewall allows TCP connections on ports 2323, 2324, 2325

### Commands not working
1. Ensure you're connected to the Command port (2323)
2. Try typing `help` to see available commands
3. Check debug port (2324) for any error messages

### Alerts not appearing
1. Ensure system events are being logged with `LOG_SYSTEM_ALERT()` and `LOG_SYSTEM_EVENT()`
2. Check if the device has actually generated alerts (feeding, alerts, etc.)
3. Verify connection to system port (2325)

## Example Client Script (Python)

```python
#!/usr/bin/env python3
import socket
import sys

def connect_to_port(host, port, name):
    """Connect to a specific console port"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    print(f"Connected to {name} (Port {port})")
    
    try:
        while True:
            data = sock.recv(1024)
            if not data:
                break
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
    except KeyboardInterrupt:
        print(f"\nDisconnected from {name}")
    finally:
        sock.close()

if __name__ == '__main__':
    host = sys.argv[1] if len(sys.argv) > 1 else '192.168.0.102'
    port_type = sys.argv[2] if len(sys.argv) > 2 else 'debug'
    
    ports = {
        'command': 2323,
        'debug': 2324,
        'system': 2325
    }
    
    if port_type not in ports:
        print(f"Unknown port type: {port_type}")
        sys.exit(1)
    
    connect_to_port(host, ports[port_type], port_type)
```

Usage:
```bash
python monitor.py 192.168.0.102 debug    # Monitor debug logs
python monitor.py 192.168.0.102 system   # Monitor system alerts
python monitor.py 192.168.0.102 command  # Send commands
```

## Summary

| Port | Purpose | Direction | Use Case |
|------|---------|-----------|----------|
| **2323** | Commands | Bi-directional | Send commands, receive responses |
| **2324** | Debug | Output only | View all logs and debug info |
| **2325** | System | Output only | Monitor critical alerts and events |

The three-port design significantly improves the developer experience by allowing focused monitoring and cleaner data streams!
