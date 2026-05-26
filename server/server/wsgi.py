import os
from pathlib import Path

try:
	from dotenv import load_dotenv
except ImportError:  # pragma: no cover - optional during local bootstrap
	load_dotenv = None

from django.core.wsgi import get_wsgi_application

BASE_DIR = Path(__file__).resolve().parent.parent

if load_dotenv is not None:
	load_dotenv(BASE_DIR / '.env')

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'server.settings')

application = get_wsgi_application()
