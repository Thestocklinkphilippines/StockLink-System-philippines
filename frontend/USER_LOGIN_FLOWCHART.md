# User Login Flowchart

This document focuses only on the user login flow, from form input in the React app to authentication decisions in the Django backend.

## 1. End-to-end login flow

```mermaid
flowchart TD
    A[User opens /login] --> B[Login page renders form]
    B --> C[User enters identity and password]
    C --> D[Submit form]
    D --> E[Login.js submit handler]
    E --> F[api.postJSON /api/auth/login/]
    F --> G[Django LoginUserView]

    G --> H{identity and password provided?}
    H -- No --> I[400 username and password required]
    H -- Yes --> J[Find user by username; fallback to email]

    J --> K{User exists and password matches?}
    K -- No --> L[401 invalid credentials]
    K -- Yes --> M{Email verified? user.is_active}
    M -- No --> N[403 Email not verified]
    M -- Yes --> O{Admin approval record exists and approved?}
    O -- No --> P[403 Awaiting admin approval]
    O -- Yes --> Q[authenticate + login session]
    Q --> R[200 user access payload]

    I --> S[Frontend shows error text]
    L --> S
    N --> S
    P --> S
    R --> T{res.ok ?}
    T -- Yes --> U[navigate /dashboard/overview]
    T -- No --> S
```

## 2. Frontend login state flow (`src/pages/Login.js`)

```mermaid
flowchart LR
    A[Form controls]
    A --> B[identity state]
    A --> C[password state]
    A --> D[show/hide password state]
    A --> E[remember checkbox state]

    F[Submit click] --> G[setError empty]
    G --> H[setIsSubmitting true]
    H --> I[POST /api/auth/login/]
    I --> J{response ok?}
    J -- Yes --> K[navigate to dashboard]
    J -- No --> L[setError from response.detail]
    K --> M[setIsSubmitting false]
    L --> M
```

Notes:
- The button is disabled while submitting, and displays `Logging in...`.
- Error messages are displayed through `auth-error` when backend returns non-OK responses.

## 3. Request/response sequence diagram

```mermaid
sequenceDiagram
    participant U as User
    participant FE as Login.js
    participant API as api.postJSON
    participant BE as Django /api/auth/login/

    U->>FE: Enter identity + password
    U->>FE: Submit form
    FE->>API: postJSON('/api/auth/login/', payload)
    API->>BE: POST JSON + CSRF + session credentials
    BE-->>API: 200/400/401/403 + JSON body
    API-->>FE: {ok, status, body}

    alt ok === true
        FE->>FE: navigate('/dashboard/overview')
    else ok === false
        FE->>FE: setError(body.detail || 'Login failed')
    end
```

## 4. Backend login decision table

| Condition | Backend response | Frontend behavior |
| --- | --- | --- |
| Missing identity or password | 400 | Show returned detail as error |
| Wrong credentials | 401 | Show returned detail as error |
| Not email verified | 403 | Show returned detail as error |
| Not admin approved | 403 | Show returned detail as error |
| Valid and approved user | 200 | Redirect to dashboard |

## 5. Source files for this flow

- [frontend/src/pages/Login.js](src/pages/Login.js)
- [frontend/src/api.js](src/api.js)
- [server/iot/views.py](../server/iot/views.py)
