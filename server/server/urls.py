from django.urls import path, include, re_path
from django.contrib import admin
from django.views.generic import TemplateView

urlpatterns = [
    path('admin/', admin.site.urls),
    path('api/', include('iot.urls')),
    # Serve the React SPA index for the root and any non-API path
    path('', TemplateView.as_view(template_name='index.html'), name='spa-index'),
    re_path(r'^(?!api/).*$', TemplateView.as_view(template_name='index.html')),
]
