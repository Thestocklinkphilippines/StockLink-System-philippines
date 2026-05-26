# Server High-Level Flowchart (User Perspective)

This map describes the server as if you are talking to it while you use the website.

## 1. Big Picture: "I request, the server decides, the system acts"

```mermaid
flowchart TD
    U[You the Website User] --> G[Gateway and URL Router]
    G --> M[Middleware Team]
    M --> A[Application Logic Brain]
    A --> D[Data Memory Layer]
    A --> B[Background Watchers]
    A --> R[Response Builder]
    R --> U

    M1[Security and Session Guard] --> M
    M2[CSRF and Auth Checker] --> M

    A1[Auth and Account Decisions] --> A
    A2[Schedules and Device Commands] --> A
    A3[Alerts Logs and Monitoring Rules] --> A
    A4[Admin and System Settings Rules] --> A

    D1[SQLite Database] --> D
    D2[Users Devices Schedules] --> D
    D3[Alerts Logs Events Settings] --> D

    B1[Connection Monitor Loop] --> B
    B2[Shutdown Alert Cleanup Task] --> B
```

## 2. What Happens To Your Request Inside the Server

```mermaid
flowchart LR
    A[You send a request] --> B[Server says Hello I got it]
    B --> C[Security check Who are you]
    C --> D{Are credentials and session valid}
    D -- No --> E[Server replies Access denied with reason]
    D -- Yes --> F[Router picks the correct endpoint]
    F --> G[View and serializer validate your input]
    G --> H{Is input valid and allowed}
    H -- No --> I[Server replies Validation error]
    H -- Yes --> J[Business rules execute]
    J --> K[Read or write database state]
    K --> L[Server replies JSON success payload]
```

## 3. High-Level Domain Journey From Your Perspective

```mermaid
flowchart TD
    U[You interact with the website] --> X{What are you trying to do}

    X --> A[Sign in or manage account]
    X --> B[Manage schedules or feed actions]
    X --> C[Review alerts logs and status]
    X --> D[Admin-level controls]

    A --> A1[Auth endpoints process identity approval and session]
    B --> B1[Device endpoints sync config and command state]
    C --> C1[Monitoring endpoints return events alerts and logs]
    D --> D1[Admin endpoints enforce role-protected actions]

    A1 --> Z[Shared rule engine persists state and responds]
    B1 --> Z
    C1 --> Z
    D1 --> Z
```

## 4. Server Side Processes Running Even When You Are Idle

```mermaid
flowchart TD
    A[Server starts] --> B[App ready hook initializes services]
    B --> C[Connection watcher wakes up on interval]
    C --> D[Checks recent device heartbeat timestamps]
    D --> E{Any stale or disconnected devices}
    E -- Yes --> F[Server marks device disconnected and logs alert]
    E -- No --> G[Server keeps current status]
    F --> C
    G --> C

    H[Maintenance command runs] --> I[Server resolves stale shutdown alerts]
```

## 5. Human-Style Summary

- You ask for an action through the frontend.
- The server verifies who you are, validates your request, applies system rules, updates data, and responds.
- In parallel, background watchers keep device health and alert state accurate even without user traffic.
