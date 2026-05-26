# PythonAnywhere Deployment

This project is set up to run the Django backend from `server/` and serve the React build from `frontend/build/`.

## 1. Create the PythonAnywhere web app

Use a Python 3.11 web app and point the source directory at the backend folder:

`/home/<your-username>/THESIS/server`

Set the WSGI file to the backend WSGI module already in the repo:

`/home/<your-username>/THESIS/server/server/wsgi.py`

## 2. Set environment variables

Add these in the PythonAnywhere Web tab or in the console session used by the app:

```bash
DJANGO_DEBUG=False
DJANGO_SECRET_KEY=replace-with-a-long-random-secret
DJANGO_ALLOWED_HOSTS=<your-username>.pythonanywhere.com
DJANGO_CSRF_TRUSTED_ORIGINS=https://<your-username>.pythonanywhere.com
```

If you serve the frontend from a separate domain, also set:

```bash
DJANGO_CORS_ALLOWED_ORIGINS=https://<your-frontend-domain>
```

## 3. Install dependencies

From a PythonAnywhere Bash console:

```bash
cd ~/THESIS/server
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

## 4. Build the frontend

The Django app serves the React build output, so build the frontend before reloading the site:

```bash
cd ~/THESIS/frontend
npm install
npm run build
```

## 5. Run migrations and collect static files

```bash
cd ~/THESIS/server
source venv/bin/activate
python manage.py migrate
python manage.py collectstatic --noinput
```

## 6. Configure static files

Map `/static/` to the backend static output directory:

`/home/<your-username>/THESIS/server/staticfiles/`

The React build assets are picked up from `frontend/build/static/` by Django settings.

## 7. Reload

Reload the web app after the build, migration, and static steps finish.

## Notes

- The app uses SQLite by default, which is fine for a small deployment or prototype.
- If you want persistent production storage on PythonAnywhere, swap to a hosted database later and migrate the settings accordingly.