# Multi-Port ESP32 Console Architecture Diagram

## Network Topology

```
                        WiFi Network
                             |
                    [ESP32 Smart Feeder]
                             |
              _______________|_______________
             |               |               |
          PORT 2323       PORT 2324       PORT 2325
          (Commands)      (Debug)         (System)
             |               |               |
        [Command        [All Logs]      [Alerts
         Client]        [Verbose]        Only]
```

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32 Firmware                       │
└─────────────────────────────────────────────────────────┘
         |                    |                   |
         |                    |                   |
         ↓                    ↓                   ↓
    Commands               Debug Output       System Alerts
    (stdin)                (stdout)            (critical)
         |                    |                   |
         ↓                    ↓                   ↓
    Parser &            LOG_DEBUG()         LOG_SYSTEM_*()
    Executor            LOG_INFO()          (Critical only)
         |                LOG_WARN()
         |                LOG_ERROR()
         ↓                Serial.print()
    Response                  |
    Output                    ↓
         |            Format & Buffer
         |                    |
    ┌────┴────┐    ┌─────────┴──────────┐
    |          |    |                    |
    ↓          ↓    ↓                    ↓
  2323       2324  2325                USB
 COMMAND    DEBUG  SYSTEM             Serial
 CONSOLE   CONSOLE CONSOLE           (Mirror)
    |        |        |                |
    ↓        ↓        ↓                ↓
 [Client]  [Client] [Client]      [USB Port]
```

## Port Characteristics Matrix

```
┌──────────┬──────────┬───────────┬──────────┬────────────┐
│   Port   │ Content  │ Direction │ Clients  │  Latency   │
├──────────┼──────────┼───────────┼──────────┼────────────┤
│  2323    │Commands  │ Bi-Dir    │ 1-2      │ <1ms       │
│  2324    │Logs      │ Output    │ 1-2      │ <1ms       │
│  2325    │Alerts    │ Output    │ 1-2      │ <1ms       │
│  USB     │All       │ Bi-Dir    │ 1        │ <5ms       │
└──────────┴──────────┴───────────┴──────────┴────────────┘
```

## Example Usage Scenarios

### Scenario A: Single Terminal (All Output)
```
                        WiFi Network
                             |
                    [ESP32 Smart Feeder]
                             |
              _______________|_____________
             |               |            |
          2323            2324          2325
             |               |            |
             └─────┬─────────┴────┬───────┘
                   |              |
              [Combined    (unused)
               Monitor]
              [nc 2323]
```

### Scenario B: Three Terminal Setup (Recommended)
```
                        WiFi Network
                             |
                    [ESP32 Smart Feeder]
                             |
              _______________|_____________
             |               |            |
          2323            2324          2325
             |               |            |
        [Terminal 1]   [Terminal 2]  [Terminal 3]
        [Commands]     [Logs/Debug]   [Alerts]
        [$ nc :2323]   [$ nc :2324]  [$ nc :2325]
```

### Scenario C: Monitoring Only
```
                        WiFi Network
                             |
                    [ESP32 Smart Feeder]
                             |
              _______________|_____________
             |               |            |
          2323            2324          2325
             |               |            |
           X           [Terminal 1]    [Terminal 2]
        (unused)       [Logs/Debug]    [Alerts/Events]
                       [nc :2324]      [nc :2325]
```

## Console Content Classification

```
Port 2323 (Commands) - Bi-Directional
├─ Command Input
│  ├─ help
│  ├─ dump_cfg
│  ├─ sched_run 1
│  └─ ... (user commands)
│
└─ Command Responses
   ├─ Help text
   ├─ Configuration dumps
   ├─ Execution results
   └─ Status information

Port 2324 (Debug) - Output Only
├─ Debug Level Logs
│  ├─ [DBG ] Detailed execution trace
│  ├─ [INFO] Informational messages
│  ├─ [WARN] Warning conditions
│  └─ [ERR ] Error conditions
│
├─ System Output
│  ├─ Serial.print() output
│  ├─ Serial.printf() output
│  └─ General logging
│
└─ Verbose Information
   ├─ Sensor readings
   ├─ Network status
   ├─ State changes
   └─ Debug traces

Port 2325 (System/Alert) - Output Only
├─ Critical Alerts
│  ├─ [ALERT] Low battery
│  ├─ [ALERT] Low feed
│  ├─ [ALERT] Low water
│  └─ [ALERT] Critical errors
│
└─ System Events
   ├─ [EVENT] Feeding started/stopped
   ├─ [EVENT] Watering started/stopped
   ├─ [EVENT] WiFi reconnected
   └─ [EVENT] Configuration updated
```

## Message Routing Logic

```
Source Code Call
    |
    v
┌─────────────────────────────────────────────┐
│ Which Function Called?                      │
├─────────────────────────────────────────────┤
│ Serial.print()           ──→ Port 2324      │
│ LOG_DEBUG/INFO/WARN/ERR  ──→ Port 2324      │
│ LOG_SYSTEM_ALERT()       ──→ Port 2325      │
│ LOG_SYSTEM_EVENT()       ──→ Port 2325      │
│ handleSerialCommands()   ──→ Port 2323      │
│ Command response         ──→ Port 2323      │
│ SFMultiConsole.xyz()     ──→ Port based     │
└─────────────────────────────────────────────┘
    |
    v
Create Formatted Message
    |
    v
Add to Output Buffer
    |
    v
┌─────────────────────────────────────────────┐
│ Send to Connected Clients (1-2 per port)    │
│ + USB Serial Mirror                         │
└─────────────────────────────────────────────┘
    |
    v
TCP Stream to Client Terminal
```

## Memory Layout (Simplified)

```
ESP32 RAM (320 KB Total)
┌─────────────────────────────────────┐
│  Code + Constants       (~150 KB)    │
├─────────────────────────────────────┤
│  Global Variables       (~30 KB)     │
├─────────────────────────────────────┤
│  WiFiServer x3          (~1.5 KB)    │  ← New
│  WiFiClient x6          (~1.5 KB)    │  ← New
├─────────────────────────────────────┤
│  Heap/Stack             (~130 KB)    │
└─────────────────────────────────────┘
     +3 KB overhead for multi-console
```

## State Machine: Client Lifecycle

```
                    ┌─────────────────┐
                    │   No Client     │
                    └────────┬────────┘
                             │
                    WiFi conn available
                             │
                             v
                    ┌─────────────────┐
                    │ Waiting for     │
                    │ Connection      │
                    │ (Server Listening)
                    └────────┬────────┘
                             │
                    Client connects
                             │
                             v
                    ┌─────────────────┐
                    │ Send Banner     │
                    │ to Client       │
                    └────────┬────────┘
                             │
                             v
                    ┌─────────────────┐
                    │  Client Active  │◄─────┐
                    │  (Streaming     │      │
                    │   data)         │      │
                    └────────┬────────┘      │
                             │              │
                ┌────────────┴────────────┐  │
                │                        │  │
            Disconnect            Send Data │
                │                        │  │
                v                        └──┘
        ┌─────────────────┐
        │ Cleanup +       │
        │ No Client       │
        └─────────────────┘
```

## Multi-Client Handling Per Port

```
WiFiServer (Port 2324)
│
├─ Slot 1: [WiFiClient] Connected ──→ Stream data
├─ Slot 2: [WiFiClient] Disconnected (cleaned up)
│
If new connection arrives:
├─ Insert into empty slot, or
└─ Replace oldest disconnected slot
```

## Performance Characteristics

```
Operation              Time         Impact
────────────────────────────────────────────
Accept new client      <1ms         Minimal
Send 512B message      <1ms         Minimal
Broadcast 1 msg/port   <1ms         Minimal
Check WiFi status      <1ms         Minimal
Service per loop       <5ms         Acceptable
────────────────────────────────────────────

Network Latency
└─ Local WiFi (same network): <5ms
└─ Internet (remote):         +100-500ms
```

## Configuration Summary

```cpp
// In sf_multi_console.h
#define MULTI_CONSOLE_COMMAND_PORT 2323  // Configurable
#define MULTI_CONSOLE_DEBUG_PORT   2324  // Configurable
#define MULTI_CONSOLE_SYSTEM_PORT  2325  // Configurable

// In class definition
static const int kMaxClientsPerPort = 2;      // Configurable
static const int kBufferSizePrintf = 512;     // In .cpp

// Port characteristics
TCP_NO_DELAY = true;              // Low latency
AUTO_SERVER_START = on_wifi_conn;  // Automatic
AUTO_CLIENT_CLEANUP = on_disconnect;
```

## Migration from Single-Port

```
Before (Single Port Design)
┌──────────────────────────┐
│ Port 2323 (Everything)   │
├──────────────────────────┤
│ Commands mixed with logs │
│ Hard to parse            │
│ Client competes for I/O  │
└──────────────────────────┘

After (Multi-Port Design)
┌──────────────────────────┐
│ Port 2323 (Commands)     │  ← Dedicated
│ Port 2324 (Debug)        │  ← Dedicated
│ Port 2325 (Alerts)       │  ← Dedicated
├──────────────────────────┤
│ Clean separation         │
│ Easy to parse            │
│ Multiple simultaneous    │
└──────────────────────────┘
```

---

**Key Insight**: By using separate TCP ports instead of complex routing,
we achieve clean separation with minimal overhead and maximum clarity!
