from django.urls import path
from . import views

urlpatterns = [
    path('system/settings/', views.SystemSettingsView.as_view(), name='system-settings'),
    path('notifications/test-email/', views.TestEmailView.as_view(), name='notifications-test-email'),
    path('devices/', views.DeviceListView.as_view(), name='device-list'),
    path('device/<str:device_id>/config/', views.DeviceConfigView.as_view(), name='device-config'),
    path('device/<str:device_id>/logs/', views.LogsView.as_view(), name='device-logs'),
    path('device/<str:device_id>/alerts/', views.AlertsView.as_view(), name='device-alerts'),
    path('device/<str:device_id>/sensor-state/', views.SensorStateView.as_view(), name='device-sensor-state'),
    path('device/<str:device_id>/feed-now/', views.FeedNowCommandView.as_view(), name='device-feed-now'),
    path('device/<str:device_id>/feed-now/<int:command_id>/ack/', views.FeedNowAcknowledgeView.as_view(), name='device-feed-now-ack'),
    path('devices/register/', views.DeviceRegisterView.as_view(), name='device-register'),
    path('auth/register/', views.RegisterUserView.as_view(), name='user-register'),
    path('auth/login/', views.LoginUserView.as_view(), name='user-login'),
    path('auth/logout/', views.LogoutUserView.as_view(), name='user-logout'),
    path('auth/user/', views.CurrentUserView.as_view(), name='user-current'),
    path('device/<str:device_id>/schedules/', views.ScheduleListCreateView.as_view(), name='device-schedules'),
    path('device/<str:device_id>/schedules/<int:pk>/', views.ScheduleDetailView.as_view(), name='device-schedule-detail'),
]
