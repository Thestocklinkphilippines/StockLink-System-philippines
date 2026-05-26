import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent


def _env_list(name, default=None):
    raw_value = os.getenv(name, "")
    values = [item.strip() for item in raw_value.split(",") if item.strip()]
    if values:
        return values
    return list(default or [])


def _env_bool(name, default=False):
    raw_value = os.getenv(name)
    if raw_value is None:
        return default
    return raw_value.strip().lower() in {"1", "true", "yes", "on"}

SECRET_KEY = os.getenv('DJANGO_SECRET_KEY', 'dev-placeholder')

DEBUG = _env_bool('DJANGO_DEBUG', True)

ALLOWED_HOSTS = _env_list('DJANGO_ALLOWED_HOSTS', ['*'] if DEBUG else [])

CSRF_TRUSTED_ORIGINS = _env_list('DJANGO_CSRF_TRUSTED_ORIGINS')

INSTALLED_APPS = [
    'django.contrib.admin',
    'django.contrib.auth',
    'django.contrib.contenttypes',
    'django.contrib.sessions',
    'django.contrib.messages',
    'django.contrib.staticfiles',
    'rest_framework',
    'iot',

    "corsheaders",
]

MIDDLEWARE = [
    'django.middleware.security.SecurityMiddleware',
    'django.contrib.sessions.middleware.SessionMiddleware',
    'corsheaders.middleware.CorsMiddleware',
    'iot.middleware.SystemTimezoneMiddleware',
    'django.middleware.common.CommonMiddleware',
    'django.middleware.csrf.CsrfViewMiddleware',
    'django.contrib.auth.middleware.AuthenticationMiddleware',
    'django.contrib.messages.middleware.MessageMiddleware',
    'django.middleware.clickjacking.XFrameOptionsMiddleware',
]

ROOT_URLCONF = 'server.urls'

TEMPLATES = [
    {
        'BACKEND': 'django.template.backends.django.DjangoTemplates',
        'DIRS': [],
        'APP_DIRS': True,
        'OPTIONS': {'context_processors': [
            'django.template.context_processors.debug',
            'django.template.context_processors.request',
            'django.contrib.auth.context_processors.auth',
            'django.contrib.messages.context_processors.messages',
        ]},
    },
]

WSGI_APPLICATION = 'server.wsgi.application'

DATABASES = {
    'default': {
        'ENGINE': 'django.db.backends.sqlite3',
        'NAME': BASE_DIR / 'db.sqlite3',
    }
}

AUTH_PASSWORD_VALIDATORS = []

LANGUAGE_CODE = 'en-us'

TIME_ZONE = 'Asia/Manila'

USE_I18N = True
USE_L10N = True
USE_TZ = True

STATIC_URL = '/static/'

# Frontend build integration (React)
FRONTEND_BUILD_DIR = BASE_DIR.parent / 'frontend' / 'build'
STATICFILES_DIRS = []
if (FRONTEND_BUILD_DIR / 'static').exists():
    STATICFILES_DIRS.append(str(FRONTEND_BUILD_DIR / 'static'))

# Allow Django template loader to find the SPA index.html
if FRONTEND_BUILD_DIR.exists():
    TEMPLATES[0]['DIRS'].append(str(FRONTEND_BUILD_DIR))

STATIC_ROOT = BASE_DIR / 'staticfiles'

REST_FRAMEWORK = {
    'DEFAULT_AUTHENTICATION_CLASSES': [
        'rest_framework.authentication.SessionAuthentication',
        'rest_framework.authentication.BasicAuthentication',
    ],
}

CORS_ALLOWED_ORIGINS = _env_list('DJANGO_CORS_ALLOWED_ORIGINS')
CORS_ALLOW_ALL_ORIGINS = DEBUG and not CORS_ALLOWED_ORIGINS

SECURE_PROXY_SSL_HEADER = ('HTTP_X_FORWARDED_PROTO', 'https')
SECURE_SSL_REDIRECT = _env_bool('DJANGO_SECURE_SSL_REDIRECT', False)
SESSION_COOKIE_SECURE = _env_bool('DJANGO_SESSION_COOKIE_SECURE', False)
CSRF_COOKIE_SECURE = _env_bool('DJANGO_CSRF_COOKIE_SECURE', False)
SECURE_HSTS_SECONDS = int(os.getenv('DJANGO_SECURE_HSTS_SECONDS', '0'))
SECURE_HSTS_INCLUDE_SUBDOMAINS = _env_bool('DJANGO_SECURE_HSTS_INCLUDE_SUBDOMAINS', False)
SECURE_HSTS_PRELOAD = _env_bool('DJANGO_SECURE_HSTS_PRELOAD', False)

# Gmail SMTP (PythonAnywhere free-tier compatible)
EMAIL_BACKEND = os.getenv('EMAIL_BACKEND', 'django.core.mail.backends.smtp.EmailBackend')
EMAIL_HOST = os.getenv('EMAIL_HOST', 'smtp.gmail.com')
EMAIL_PORT = int(os.getenv('EMAIL_PORT', '587'))
EMAIL_USE_TLS = os.getenv('EMAIL_USE_TLS', 'true').lower() == 'true'
EMAIL_HOST_USER = os.getenv('EMAIL_HOST_USER', '')
EMAIL_HOST_PASSWORD = os.getenv('EMAIL_HOST_PASSWORD', '')
DEFAULT_FROM_EMAIL = os.getenv('DEFAULT_FROM_EMAIL', EMAIL_HOST_USER or 'no-reply@stocklink.local')
EMAIL_TIMEOUT = int(os.getenv('EMAIL_TIMEOUT', '20'))
