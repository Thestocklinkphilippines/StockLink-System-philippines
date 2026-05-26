# Multi-Port Console: Practical Examples

## Example 1: Basic Setup - Three Terminal Monitoring

### Setup
```bash
# Terminal 1: Send commands and see responses
ssh user@pc  # (on your development machine)
nc 192.168.0.102 2323

# Terminal 2: Watch all debug output
ssh user@pc
nc 192.168.0.102 2324

# Terminal 3: Watch critical alerts
ssh user@pc
nc 192.168.0.102 2325
```

### What You'll See

**Terminal 1 (Commands - Port 2323)**:
```
=== ESP32 Command Console (Port 2323) ===
Send commands here. Responses appear on this port.
Type 'help' for available commands.

> help
================ SMART FEEDER SERIAL COMMANDS ================
help         -> show this command list
dump_cfg     -> print full local JSON config from Preferences
dump_state   -> print runtime state snapshot
sched_list   -> list schedules from local config
sched_run    -> run schedule now (ignore time): sched_run [id]
...
> dump_state
----- BEGIN ESP32 RUNTIME STATE -----
wifi_connected=1
wifi_ip=192.168.0.102
mains_power_present=1
remaining_kg=2.457
buffered_event_count=0
uptime_ms=45230
----- END ESP32 RUNTIME STATE -----
```

**Terminal 2 (Debug - Port 2324)**:
```
=== ESP32 Debug Console (Port 2324) ===
All debug logs and general output appear here.

[INFO] Smart feeder booting
[INFO] Build mode simulation=1 verbose=0
[INFO] Pins initialized
[INFO] I2C initialized SDA=21 SCL=22
[INFO] LCD initialized addr=0x27 cols=20 rows=4
[INFO] WiFi connected to 'HomeNetwork' IP=192.168.0.102
[INFO] Time ready epoch=1715000000
[INFO] Setup complete mainsPresent=1
[DBG ] Sensor reading: feeder=85.2% water=92.5%
```

**Terminal 3 (System/Alert - Port 2325)**:
```
=== ESP32 System Console (Port 2325) ===
Critical system events and alerts appear here.

[EVENT] WiFi connected IP=192.168.0.102
[EVENT] System online and ready
```

---

## Example 2: Trigger a Feeding Event

### Step 1: Get Available Schedules
```bash
# Terminal 1
> sched_list
Schedules (local config):
  [1] id=101 enabled=1 time=14:30 amount=0.500 name=Morning Feed
  [2] id=102 enabled=1 time=18:00 amount=0.250 name=Evening Feed
  [3] id=103 enabled=0 time=22:00 amount=0.100 name=Night Feed
```

### Step 2: Run Schedule #1
```bash
# Terminal 1
> sched_run 101
Manual run schedule id=101 name=Morning Feed time=14:30 amount=0.500 (time ignored)
Manual schedule run complete.
```

### Step 3: Observe Across All Terminals

**Terminal 1 Response** (Command responses):
```
> sched_run 101
Manual run schedule id=101 name=Morning Feed time=14:30 amount=0.500 (time ignored)
Manual schedule run complete.
```

**Terminal 2 Debug Output** (Detailed logs):
```
[INFO] Serial command received: sched_run 101
[INFO] Manual run schedule id=101 name=Morning Feed amount=0.500
[DBG ] Checking feed sufficiency: required=0.500 available=2.457
[DBG ] Feed motor start: millis=45230
[DBG ] Motor running at power level 100%
[DBG ] Motor off after 1250ms
[DBG ] Feed dispensed, calculating remaining
[INFO] Feed dispensed successfully: 0.500kg
[DBG ] Remaining feed updated: 1.957kg
```

**Terminal 3 System Alerts** (Events):
```
[EVENT] Feeding started: schedule_id=101 amount=0.500kg
[EVENT] Feed motor activated
[EVENT] Feed motor deactivated after 1250ms
[EVENT] Feed dispensed: 0.500kg remaining=1.957kg
```

---

## Example 3: Debug a WiFi Reconnection

### Setup: Monitor Both Debug and System
```bash
# Terminal 1: Debug details
nc 192.168.0.102 2324

# Terminal 2: System events
nc 192.168.0.102 2325

# Terminal 3: Manual control
nc 192.168.0.102 2323
```

### Trigger WiFi Restart
```bash
# (Manually restart WiFi or unplug/replug network cable)
```

### Observe the Sequence

**Terminal 1 (Debug - Detailed)**:
```
[WARN] WiFi disconnected
[DBG ] WiFi status changed to: WL_DISCONNECTED
[INFO] Attempting WiFi reconnection...
[DBG ] WiFi scan started
[DBG ] WiFi scan found 8 networks
[DBG ] Connecting to 'HomeNetwork'...
[DBG ] WiFi status: WL_CONNECTING
[DBG ] WiFi status: WL_CONNECTED
[INFO] WiFi connected to 'HomeNetwork' IP=192.168.0.102
[DBG ] DHCP lease acquired
[INFO] Resuming cloud sync service
```

**Terminal 2 (System - Events)**:
```
[ALERT] WiFi disconnected - services paused
[EVENT] WiFi reconnection started
[EVENT] WiFi connected IP=192.168.0.102
[ALERT] Services resumed after reconnection
```

**Terminal 3 (Commands - Still works)**:
```
> dump_state
----- BEGIN ESP32 RUNTIME STATE -----
wifi_connected=1
wifi_ip=192.168.0.102
mains_power_present=1
...
----- END ESP32 RUNTIME STATE -----
```

---

## Example 4: Detect Low Battery Condition

### Background
Device has battery monitoring enabled and threshold is 9.0V.

### Watch for Low Battery

```bash
# Terminal 1: System alerts (perfect for this!)
nc 192.168.0.102 2325
```

### Battery Dies Gradually

**Terminal 1 Output (in real-time)**:
```
[EVENT] Battery voltage: 12.4V (normal)
[EVENT] Battery voltage: 11.2V (normal)
[EVENT] Battery voltage: 9.8V (normal)
[EVENT] Battery voltage: 9.1V (normal)
[ALERT] Low battery detected voltage=8.9V threshold=9.0V
[ALERT] Preparing emergency shutdown sequence
[ALERT] Low battery shutdown relay asserted at 8.9V
[EVENT] Device shutdown initiated
```

### View Full Details in Debug

```bash
# Terminal 2: Debug output (if you want more details)
nc 192.168.0.102 2324
```

**Terminal 2 Debug Output**:
```
[DBG ] Battery ADC reading: 2850 (8.9V)
[WARN] Low battery detected batt=8.9V threshold=9.0V; preparing shutdown
[DBG ] Sending low_battery_shutdown alert to cloud
[DBG ] Buffering offline event for later sync
[INFO] Low battery shutdown relay asserted at 8.9V
[DBG ] Device will power down in 30 seconds
[DBG ] Shutdown relay GPIO pin 4 set HIGH
```

---

## Example 5: Keypad Calibration Session

### Start Calibration
```bash
# Terminal: Commands
nc 192.168.0.102 2323

> keypad_cal
```

### What Happens

**Terminal 1 Output**:
```
[KEYPAD CAL] Serial output is now in exclusive calibration mode.
[KEYPAD CAL] Normal logs are temporarily muted.
[KEYPAD CAL] Commands during calibration: sample | cancel

[KEYPAD CAL] Step 1/16
[KEYPAD CAL] Press and hold key '1', then type: sample
[KEYPAD CAL] Type 'cancel' anytime to abort.

> sample
[KEYPAD CAL] Captured key '1' -> ADC=2048

[KEYPAD CAL] Step 2/16
[KEYPAD CAL] Press and hold key '2', then type: sample

> sample
[KEYPAD CAL] Captured key '2' -> ADC=2010

... (continue for all 16 keys) ...

[KEYPAD CAL] Final step
[KEYPAD CAL] Release all keys, then type: sample

> sample
[KEYPAD CAL] Saved. idle=low no_key_adc=3000 trend=descending
[KEYPAD CAL] Calibration complete. Returning to normal operation.
```

### During Calibration
- **Port 2323**: Exclusive mode (shows calibration prompts)
- **Port 2324**: Silent (normal logs muted)
- **Port 2325**: Silent (no alerts during cal)

### After Calibration
- All ports resume normal operation
- Calibration data saved to device

---

## Example 6: Monitor Water Level Changes

### Setup
```bash
# Watch for water events
nc 192.168.0.102 2325

# Also watch debug (optional)
nc 192.168.0.102 2324
```

### Water Level Drop Detected

**Terminal 1 (System/Alerts)**:
```
[EVENT] Water level: 87% (normal)
[EVENT] Water level: 45% (monitoring)
[EVENT] Water level: 15% (monitoring)
[ALERT] Low water detected! (below 20%)
[EVENT] Water refill pump activated
[EVENT] Water refill in progress (15% -> 95%)
[EVENT] Water refill pump deactivated
[EVENT] Water level: 95% (nominal)
```

**Terminal 2 (Debug - if enabled)**:
```
[DBG ] Water sensor reading: 87% distance=15cm
[DBG ] Water sensor reading: 45% distance=28cm
[DBG ] Water sensor reading: 15% distance=42cm
[WARN] Water level critical: 15% (below threshold 20%)
[INFO] Water refill activated
[DBG ] Solenoid valve GPIO 26 set HIGH
[DBG ] Water refill timer started
[DBG ] Water level increasing: 15% -> 45% -> 95%
[INFO] Water refill complete
[DBG ] Solenoid valve GPIO 26 set LOW
```

---

## Example 7: Python Monitoring Script

Create `monitor.py`:

```python
#!/usr/bin/env python3
"""
Multi-port ESP32 console monitor
Monitors all three ports simultaneously and colors output
"""

import socket
import sys
import threading
import time
from datetime import datetime

class ConsoleMonitor:
    def __init__(self, host, ports):
        self.host = host
        self.ports = ports  # {'name': port_num}
        self.sockets = {}
        self.running = True
    
    def connect_port(self, name, port):
        """Connect to a specific port"""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5)
            s.connect((self.host, port))
            self.sockets[name] = s
            print(f"✓ Connected to {name:8} (port {port})")
            return True
        except Exception as e:
            print(f"✗ Failed to connect to {name}: {e}")
            return False
    
    def monitor_port(self, name, port):
        """Monitor a single port"""
        if not self.connect_port(name, port):
            return
        
        s = self.sockets[name]
        prefix = f"[{name:8}]"
        
        try:
            buffer = ""
            while self.running:
                try:
                    data = s.recv(1024).decode('utf-8', errors='ignore')
                    if not data:
                        print(f"{prefix} Connection closed")
                        break
                    
                    buffer += data
                    lines = buffer.split('\n')
                    
                    # Print all complete lines
                    for line in lines[:-1]:
                        if line:
                            print(f"{prefix} {line}")
                    
                    # Keep incomplete line in buffer
                    buffer = lines[-1]
                    
                except socket.timeout:
                    continue
        
        except Exception as e:
            print(f"{prefix} Error: {e}")
        finally:
            s.close()
    
    def run(self):
        """Start monitoring all ports"""
        print(f"Monitoring {self.host}...")
        print("=" * 80)
        
        threads = []
        for name, port in self.ports.items():
            t = threading.Thread(target=self.monitor_port, args=(name, port))
            t.daemon = True
            t.start()
            threads.append(t)
            time.sleep(0.2)  # Stagger connections
        
        try:
            # Keep main thread alive
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n\nShutting down...")
            self.running = False
            for t in threads:
                t.join(timeout=2)

if __name__ == '__main__':
    host = sys.argv[1] if len(sys.argv) > 1 else '192.168.0.102'
    
    ports = {
        'COMMAND': 2323,
        'DEBUG': 2324,
        'SYSTEM': 2325,
    }
    
    monitor = ConsoleMonitor(host, ports)
    monitor.run()
```

### Run It:
```bash
python monitor.py 192.168.0.102
```

### Output:
```
Monitoring 192.168.0.102...
================================================================================
✓ Connected to COMMAND  (port 2323)
✓ Connected to DEBUG    (port 2324)
✓ Connected to SYSTEM   (port 2325)
[DEBUG   ] =================================
[DEBUG   ] [INFO] Smart feeder booting
[DEBUG   ] [INFO] Build mode simulation=1
[SYSTEM  ] [EVENT] WiFi connected IP=192.168.0.102
[DEBUG   ] [INFO] Setup complete
[COMMAND ] === ESP32 Command Console (Port 2323) ===
```

---

## Example 8: Automated Testing with Telnet Script

```bash
#!/bin/bash
# test_feeder.sh - Automated test sequence

ESP32_IP="192.168.0.102"
COMMAND_PORT=2323

# Function to send command and get response
send_command() {
    local cmd=$1
    echo "Testing: $cmd"
    (sleep 0.5; echo "$cmd"; sleep 1) | nc $ESP32_IP $COMMAND_PORT
    echo "---"
}

# Run test sequence
echo "Starting feeder tests..."
send_command "help"
send_command "dump_state"
send_command "sched_list"
send_command "sched_run 1"
echo "Tests complete!"
```

### Run:
```bash
chmod +x test_feeder.sh
./test_feeder.sh
```

---

## Summary: Port Usage Patterns

| Use Case | Ports | Command |
|----------|-------|---------|
| Send commands | 2323 | `nc ip 2323` |
| Debug issue | 2323 + 2324 | `nc ip 2323` & `nc ip 2324` |
| Monitor alerts | 2325 | `nc ip 2325` |
| Full monitoring | All 3 | 3 terminals |
| Automated testing | 2323 | `(echo cmd) \| nc ip 2323` |
| Python script | All 3 | `python monitor.py ip` |

---

These examples show the power and flexibility of the three-port design!
