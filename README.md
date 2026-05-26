# IoT Smart Feeder & Drinker — Prototype

This workspace contains a minimal scaffold for the Django REST backend and a React frontend starter, plus models and example endpoints for device sync, logs, and alerts.

See `requirements.txt` for Python deps.

For PythonAnywhere deployment, see [PYTHONANYWHERE_DEPLOYMENT.md](PYTHONANYWHERE_DEPLOYMENT.md).

Quick setup (local dev):

1. Create virtualenv and install:

```bash
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
```

2. Run migrations and start server:

```bash
cd server
python manage.py migrate
python manage.py runserver
```

3. Frontend: open `frontend` and initialize with `npm install` then `npm start` (if desired).

This scaffold provides:
- Django app `iot` with models: `Device`, `DeviceConfig`, `Alert`, `Log`.
- Basic REST endpoints for device sync, logs, and alerts.
