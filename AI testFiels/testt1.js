import React, { useState, useEffect } from 'react';
import { 
  Home, Calendar, Droplets, Bell, Settings, Activity, 
  Wifi, WifiOff, AlertTriangle, CheckCircle, Clock, 
  Plus, Trash2, Edit2, Power, X, Zap, Droplet
} from 'lucide-react';

// --- MOCK API (Simulating Axios & Django REST API Endpoints) ---
// In production, replace `mockAxios` with `import axios from 'axios';`
const mockAxios = {
  get: async (url) => {
    return new Promise((resolve) => {
      setTimeout(() => {
        if (url === '/api/v1/status/') {
          resolve({ data: { status: 'Online', last_ping: 'Just now', feed_level: 12.5, feed_capacity: 20, water_level: 85, next_feed: '14:30', dispensed_today: 1.2, active_alerts: 2 } });
        } else if (url === '/api/v1/schedules/') {
          resolve({ data: [
            { id: 1, name: 'Morning Feed', days: ['Mon','Tue','Wed','Thu','Fri','Sat','Sun'], time: '08:00', amount: 0.5, enabled: true },
            { id: 2, name: 'Evening Feed', days: ['Mon','Tue','Wed','Thu','Fri','Sat','Sun'], time: '18:00', amount: 0.5, enabled: true },
            { id: 3, name: 'Midday Snack', days: ['Sat','Sun'], time: '12:30', amount: 0.2, enabled: false }
          ]});
        } else if (url === '/api/v1/water-logs/') {
          resolve({ data: [
            { id: 101, timestamp: '2026-02-25T09:15:00Z', reason: 'Low Water Threshold (15%)', status: 'Completed' },
            { id: 102, timestamp: '2026-02-24T14:20:00Z', reason: 'Manual Override', status: 'Completed' },
            { id: 103, timestamp: '2026-02-22T08:05:00Z', reason: 'Low Water Threshold (15%)', status: 'Failed - Valve Error' },
          ]});
        } else if (url === '/api/v1/alerts/') {
          resolve({ data: [
            { id: 1, type: 'motor_jam', severity: 'Critical', message: 'Auger motor jam detected during feed.', timestamp: '2026-02-25T10:20:00Z', acknowledged: false },
            { id: 2, type: 'low_feed', severity: 'Medium', message: 'Feed tank level below 20%.', timestamp: '2026-02-25T08:00:00Z', acknowledged: false },
            { id: 3, type: 'device_offline', severity: 'Low', message: 'Brief connection drop detected.', timestamp: '2026-02-24T23:15:00Z', acknowledged: true },
          ]});
        } else if (url === '/api/v1/settings/') {
          resolve({ data: { feed_capacity: 20, low_feed_threshold: 15, low_water_threshold: 20, timezone: 'Asia/Manila' } });
        }
      }, 400); // 400ms delay to simulate network
    });
  },
  post: async (url, payload) => {
    return new Promise((resolve) => {
      setTimeout(() => {
        resolve({ data: { success: true, message: 'Action triggered successfully.', ...payload } });
      }, 500);
    });
  },
  patch: async (url, payload) => {
    return new Promise((resolve) => setTimeout(() => resolve({ data: { success: true } }), 300));
  },
  delete: async (url) => {
    return new Promise((resolve) => setTimeout(() => resolve({ data: { success: true } }), 300));
  }
};

// --- COMPONENTS ---

const Card = ({ children, className = "" }) => (
  <div className={`bg-slate-800 rounded-2xl border border-slate-700/50 shadow-lg shadow-black/20 p-5 ${className}`}>
    {children}
  </div>
);

const Badge = ({ children, variant = 'info' }) => {
  const variants = {
    success: 'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20',
    warning: 'bg-amber-500/10 text-amber-400 border border-amber-500/20',
    danger: 'bg-rose-500/10 text-rose-400 border border-rose-500/20',
    info: 'bg-cyan-500/10 text-cyan-400 border border-cyan-500/20',
    neutral: 'bg-slate-500/10 text-slate-400 border border-slate-500/20',
  };
  return (
    <span className={`px-2.5 py-1 rounded-full text-xs font-medium ${variants[variant]}`}>
      {children}
    </span>
  );
};

// --- PAGES / TABS ---

const DashboardTab = () => {
  const [data, setData] = useState(null);

  useEffect(() => {
    mockAxios.get('/api/v1/status/').then(res => setData(res.data));
  }, []);

  if (!data) return <div className="p-8 text-center text-slate-400 animate-pulse">Loading telemetry...</div>;

  const feedPercent = Math.min(100, Math.round((data.feed_level / data.feed_capacity) * 100));

  return (
    <div className="space-y-6">
      <div className="flex flex-col sm:flex-row justify-between items-start sm:items-center gap-4">
        <div>
          <h2 className="text-2xl font-bold text-white tracking-tight">System Overview</h2>
          <p className="text-slate-400 text-sm">Real-time status of your smart feeder & drinker.</p>
        </div>
        <div className="flex items-center space-x-3 bg-slate-800 px-4 py-2 rounded-xl border border-slate-700">
          <div className="relative flex h-3 w-3">
            <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-75"></span>
            <span className="relative inline-flex rounded-full h-3 w-3 bg-emerald-500"></span>
          </div>
          <span className="text-sm font-medium text-emerald-400">{data.status}</span>
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6">
        {/* Feed Tank Card */}
        <Card>
          <div className="flex justify-between items-start mb-4">
            <div>
              <p className="text-sm text-slate-400 font-medium">Feed Level</p>
              <h3 className="text-2xl font-bold text-white mt-1">{data.feed_level} <span className="text-base font-normal text-slate-400">/ {data.capacity} kg</span></h3>
            </div>
            <div className="p-2 bg-cyan-500/10 rounded-lg text-cyan-400">
              <Zap size={20} />
            </div>
          </div>
          <div className="w-full bg-slate-700/50 rounded-full h-2.5 mb-1 overflow-hidden">
            <div className={`h-2.5 rounded-full ${feedPercent > 20 ? 'bg-cyan-500' : 'bg-rose-500'}`} style={{ width: `${feedPercent}%` }}></div>
          </div>
          <p className="text-xs text-right text-slate-400">{feedPercent}% Full</p>
        </Card>

        {/* Water Tank Card */}
        <Card>
          <div className="flex justify-between items-start mb-4">
            <div>
              <p className="text-sm text-slate-400 font-medium">Water Level</p>
              <h3 className="text-2xl font-bold text-white mt-1">{data.water_level}%</h3>
            </div>
            <div className="p-2 bg-blue-500/10 rounded-lg text-blue-400">
              <Droplets size={20} />
            </div>
          </div>
          <div className="w-full bg-slate-700/50 rounded-full h-2.5 mb-1 overflow-hidden">
            <div className={`h-2.5 rounded-full ${data.water_level > 20 ? 'bg-blue-500' : 'bg-rose-500'}`} style={{ width: `${data.water_level}%` }}></div>
          </div>
          <p className="text-xs text-right text-slate-400">Auto-refill standby</p>
        </Card>

        {/* Next Feeding */}
        <Card>
          <div className="flex justify-between items-start">
            <div>
              <p className="text-sm text-slate-400 font-medium">Next Feeding</p>
              <h3 className="text-2xl font-bold text-white mt-1">{data.next_feed}</h3>
            </div>
            <div className="p-2 bg-emerald-500/10 rounded-lg text-emerald-400">
              <Clock size={20} />
            </div>
          </div>
          <div className="mt-4 flex items-center text-sm text-slate-400">
            <Calendar size={14} className="mr-1.5" /> Scheduled
          </div>
        </Card>

        {/* Total Dispensed */}
        <Card>
          <div className="flex justify-between items-start">
            <div>
              <p className="text-sm text-slate-400 font-medium">Dispensed Today</p>
              <h3 className="text-2xl font-bold text-white mt-1">{data.dispensed_today} <span className="text-base font-normal text-slate-400">kg</span></h3>
            </div>
            <div className="p-2 bg-purple-500/10 rounded-lg text-purple-400">
              <Activity size={20} />
            </div>
          </div>
          <div className="mt-4 flex items-center text-sm text-emerald-400">
            <CheckCircle size={14} className="mr-1.5" /> Daily target on track
          </div>
        </Card>
      </div>
      
      {/* Quick Action / Alerts Summary */}
      {data.active_alerts > 0 && (
         <div className="bg-rose-500/10 border border-rose-500/20 rounded-xl p-4 flex items-center justify-between">
           <div className="flex items-center text-rose-400">
             <AlertTriangle className="mr-3" size={24} />
             <div>
               <h4 className="font-semibold">Attention Required</h4>
               <p className="text-sm opacity-80">You have {data.active_alerts} active system alerts.</p>
             </div>
           </div>
         </div>
      )}
    </div>
  );
};

const ScheduleManager = () => {
  const [schedules, setSchedules] = useState([]);
  const [showModal, setShowModal] = useState(false);

  useEffect(() => {
    mockAxios.get('/api/v1/schedules/').then(res => setSchedules(res.data));
  }, []);

  const toggleStatus = async (id, currentStatus) => {
    await mockAxios.patch(`/api/v1/schedules/${id}/`, { enabled: !currentStatus });
    setSchedules(schedules.map(s => s.id === id ? { ...s, enabled: !currentStatus } : s));
  };

  const deleteSchedule = async (id) => {
    await mockAxios.delete(`/api/v1/schedules/${id}/`);
    setSchedules(schedules.filter(s => s.id !== id));
  };

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2 className="text-2xl font-bold text-white">Feeding Schedules</h2>
          <p className="text-slate-400 text-sm">Manage automated feeding times.</p>
        </div>
        <button 
          onClick={() => setShowModal(true)}
          className="bg-cyan-500 hover:bg-cyan-400 text-slate-900 font-semibold px-4 py-2 rounded-xl flex items-center transition-colors"
        >
          <Plus size={18} className="mr-2" /> Add Schedule
        </button>
      </div>

      <div className="grid grid-cols-1 gap-4">
        {schedules.map(schedule => (
          <Card key={schedule.id} className={`transition-all ${!schedule.enabled ? 'opacity-60 grayscale-[50%]' : ''}`}>
            <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
              <div className="flex-1">
                <div className="flex items-center space-x-3 mb-1">
                  <h3 className="text-lg font-bold text-white">{schedule.time}</h3>
                  <span className="text-sm font-medium text-cyan-400">{schedule.name}</span>
                  {schedule.enabled ? <Badge variant="success">Active</Badge> : <Badge variant="neutral">Disabled</Badge>}
                </div>
                <div className="flex items-center space-x-4 text-sm text-slate-400">
                  <span className="flex items-center"><Zap size={14} className="mr-1" /> {schedule.amount} kg</span>
                  <span className="flex items-center"><Calendar size={14} className="mr-1" /> {schedule.days.join(', ')}</span>
                </div>
              </div>
              
              <div className="flex items-center space-x-3">
                <button 
                  onClick={() => toggleStatus(schedule.id, schedule.enabled)}
                  className={`relative inline-flex h-6 w-11 items-center rounded-full transition-colors ${schedule.enabled ? 'bg-cyan-500' : 'bg-slate-600'}`}
                >
                  <span className={`inline-block h-4 w-4 transform rounded-full bg-white transition-transform ${schedule.enabled ? 'translate-x-6' : 'translate-x-1'}`} />
                </button>
                <button className="p-2 text-slate-400 hover:text-cyan-400 bg-slate-700/50 rounded-lg transition-colors">
                  <Edit2 size={16} />
                </button>
                <button 
                  onClick={() => deleteSchedule(schedule.id)}
                  className="p-2 text-slate-400 hover:text-rose-400 bg-slate-700/50 rounded-lg transition-colors"
                >
                  <Trash2 size={16} />
                </button>
              </div>
            </div>
          </Card>
        ))}
      </div>

      {/* Add Schedule Modal (Simplified) */}
      {showModal && (
        <div className="fixed inset-0 bg-slate-900/80 backdrop-blur-sm z-50 flex items-center justify-center p-4">
          <div className="bg-slate-800 rounded-2xl border border-slate-700 p-6 w-full max-w-md shadow-2xl">
            <div className="flex justify-between items-center mb-6">
              <h3 className="text-xl font-bold text-white">Add New Schedule</h3>
              <button onClick={() => setShowModal(false)} className="text-slate-400 hover:text-white"><X size={20} /></button>
            </div>
            <div className="space-y-4">
              <div>
                <label className="block text-sm font-medium text-slate-400 mb-1">Schedule Name</label>
                <input type="text" placeholder="e.g., Afternoon Snack" className="w-full bg-slate-900 border border-slate-700 rounded-xl px-4 py-2 text-white focus:border-cyan-500 focus:outline-none" />
              </div>
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <label className="block text-sm font-medium text-slate-400 mb-1">Time</label>
                  <input type="time" className="w-full bg-slate-900 border border-slate-700 rounded-xl px-4 py-2 text-white focus:border-cyan-500 focus:outline-none [color-scheme:dark]" />
                </div>
                <div>
                  <label className="block text-sm font-medium text-slate-400 mb-1">Amount (kg)</label>
                  <input type="number" step="0.1" placeholder="0.5" className="w-full bg-slate-900 border border-slate-700 rounded-xl px-4 py-2 text-white focus:border-cyan-500 focus:outline-none" />
                </div>
              </div>
              <button 
                onClick={() => setShowModal(false)}
                className="w-full bg-cyan-500 hover:bg-cyan-400 text-slate-900 font-bold py-3 rounded-xl transition-colors mt-4"
              >
                Save Schedule
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

const WaterLogs = () => {
  const [logs, setLogs] = useState([]);

  useEffect(() => {
    mockAxios.get('/api/v1/water-logs/').then(res => setLogs(res.data));
  }, []);

  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-2xl font-bold text-white">Water Refill Logs</h2>
        <p className="text-slate-400 text-sm">History of automated and manual water refills.</p>
      </div>

      <Card className="overflow-hidden p-0">
        <div className="overflow-x-auto">
          <table className="w-full text-left border-collapse">
            <thead>
              <tr className="bg-slate-900/50 border-b border-slate-700/50 text-sm text-slate-400">
                <th className="p-4 font-medium">Date & Time</th>
                <th className="p-4 font-medium">Trigger Reason</th>
                <th className="p-4 font-medium">Status</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-700/50">
              {logs.map((log) => (
                <tr key={log.id} className="hover:bg-slate-700/20 transition-colors">
                  <td className="p-4 text-white text-sm">
                    {new Date(log.timestamp).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' })}
                  </td>
                  <td className="p-4 text-slate-300 text-sm">{log.reason}</td>
                  <td className="p-4">
                    <Badge variant={log.status.includes('Failed') ? 'danger' : 'success'}>
                      {log.status}
                    </Badge>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </Card>
    </div>
  );
};

const AlertsTab = () => {
  const [alerts, setAlerts] = useState([]);

  useEffect(() => {
    mockAxios.get('/api/v1/alerts/').then(res => setAlerts(res.data));
  }, []);

  const acknowledge = async (id) => {
    await mockAxios.patch(`/api/v1/alerts/${id}/`, { acknowledged: true });
    setAlerts(alerts.map(a => a.id === id ? { ...a, acknowledged: true } : a));
  };

  const getIcon = (type) => {
    switch(type) {
      case 'motor_jam': return <AlertTriangle size={24} />;
      case 'device_offline': return <WifiOff size={24} />;
      default: return <Bell size={24} />;
    }
  };

  const getVariant = (severity) => {
    if (severity === 'Critical') return 'danger';
    if (severity === 'Medium') return 'warning';
    return 'info';
  };

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2 className="text-2xl font-bold text-white">System Alerts</h2>
          <p className="text-slate-400 text-sm">Active notifications and warnings.</p>
        </div>
        <button className="text-sm text-slate-400 hover:text-white transition-colors border border-slate-700 rounded-lg px-3 py-1.5">
          Clear All
        </button>
      </div>

      <div className="space-y-4">
        {alerts.map(alert => (
          <Card key={alert.id} className={`flex items-start gap-4 transition-all ${alert.acknowledged ? 'opacity-50' : ''}`}>
            <div className={`p-3 rounded-xl ${
              alert.severity === 'Critical' ? 'bg-rose-500/10 text-rose-400' : 
              alert.severity === 'Medium' ? 'bg-amber-500/10 text-amber-400' : 'bg-cyan-500/10 text-cyan-400'
            }`}>
              {getIcon(alert.type)}
            </div>
            <div className="flex-1">
              <div className="flex justify-between items-start mb-1">
                <h4 className={`font-bold ${alert.acknowledged ? 'text-slate-400 line-through' : 'text-white'}`}>
                  {alert.message}
                </h4>
                <Badge variant={getVariant(alert.severity)}>{alert.severity}</Badge>
              </div>
              <p className="text-sm text-slate-400">
                {new Date(alert.timestamp).toLocaleString()}
              </p>
            </div>
            {!alert.acknowledged && (
              <button 
                onClick={() => acknowledge(alert.id)}
                className="bg-slate-700/50 hover:bg-slate-700 text-sm font-medium text-white px-3 py-1.5 rounded-lg transition-colors whitespace-nowrap"
              >
                Acknowledge
              </button>
            )}
          </Card>
        ))}
        {alerts.length === 0 && (
           <div className="text-center py-12 text-slate-500">
             <CheckCircle size={48} className="mx-auto mb-4 opacity-20" />
             <p>All clear! No active alerts.</p>
           </div>
        )}
      </div>
    </div>
  );
};

const SettingsTab = () => {
  const [loading, setLoading] = useState({ feed: false, water: false });
  const [settings, setSettings] = useState({ feed_capacity: 0, low_feed_threshold: 0, low_water_threshold: 0, timezone: '' });

  useEffect(() => {
    mockAxios.get('/api/v1/settings/').then(res => setSettings(res.data));
  }, []);

  const handleManualAction = async (type) => {
    setLoading({ ...loading, [type]: true });
    await mockAxios.post(`/api/v1/trigger/${type}/`, {});
    setLoading({ ...loading, [type]: false });
  };

  const handleSaveSettings = async (e) => {
    e.preventDefault();
    await mockAxios.patch('/api/v1/settings/', settings);
    alert("Settings saved successfully.");
  };

  return (
    <div className="space-y-6">
      <div>
        <h2 className="text-2xl font-bold text-white">System Settings</h2>
        <p className="text-slate-400 text-sm">Configure hardware parameters and manual overrides.</p>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        
        {/* Settings Form */}
        <Card className="lg:col-span-2">
          <h3 className="text-lg font-bold text-white border-b border-slate-700 pb-4 mb-4">Hardware Configuration</h3>
          <form className="space-y-4" onSubmit={handleSaveSettings}>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <div>
                <label className="block text-sm font-medium text-slate-400 mb-1">Feed Tank Capacity (kg)</label>
                <input 
                  type="number" 
                  value={settings.feed_capacity}
                  onChange={e => setSettings({...settings, feed_capacity: e.target.value})}
                  className="w-full bg-slate-900 border border-slate-700 rounded-xl px-4 py-2 text-white focus:border-cyan-500 focus:outline-none" 
                />
              </div>
              <div>
                <label className="block text-sm font-medium text-slate-400 mb-1">Low Feed Threshold (%)</label>
                <input 
                  type="number" 
                  value={settings.low_feed_threshold}
                  onChange={e => setSettings({...settings, low_feed_threshold: e.target.value})}
                  className="w-full bg-slate-900 border border-slate-700 rounded-xl px-4 py-2 text-white focus:border-cyan-500 focus:outline-none" 
                />
              </div>
              <div>
                <label className="block text-sm font-medium text-slate-400 mb-1">Low Water Threshold (%)</label>
                <input 
                  type="number" 
                  value={settings.low_water_threshold}
                  onChange={e => setSettings({...settings, low_water_threshold: e.target.value})}
                  className="w-full bg-slate-900 border border-slate-700 rounded-xl px-4 py-2 text-white focus:border-cyan-500 focus:outline-none" 
                />
              </div>
              <div>
                <label className="block text-sm font-medium text-slate-400 mb-1">Timezone</label>
                <select 
                  value={settings.timezone}
                  onChange={e => setSettings({...settings, timezone: e.target.value})}
                  className="w-full bg-slate-900 border border-slate-700 rounded-xl px-4 py-2 text-white focus:border-cyan-500 focus:outline-none"
                >
                  <option>UTC</option>
                  <option>Asia/Manila</option>
                  <option>America/New_York</option>
                </select>
              </div>
            </div>
            <div className="pt-4 flex justify-end">
              <button type="submit" className="bg-cyan-500 hover:bg-cyan-400 text-slate-900 font-bold py-2 px-6 rounded-xl transition-colors">
                Save Changes
              </button>
            </div>
          </form>
        </Card>

        {/* Manual Overrides */}
        <Card className="flex flex-col">
          <h3 className="text-lg font-bold text-white border-b border-slate-700 pb-4 mb-4">Manual Controls</h3>
          <p className="text-sm text-slate-400 mb-6">Instantly trigger the hardware systems bypassing the schedule.</p>
          
          <div className="space-y-4 mt-auto">
            <button 
              onClick={() => handleManualAction('feed')}
              disabled={loading.feed}
              className="w-full bg-slate-700 hover:bg-cyan-500/20 border border-slate-600 hover:border-cyan-500 text-white font-bold py-4 rounded-xl flex items-center justify-center transition-all group disabled:opacity-50"
            >
              <Zap size={20} className="mr-2 text-cyan-400 group-hover:animate-pulse" /> 
              {loading.feed ? 'Dispensing...' : 'Trigger Manual Feed'}
            </button>
            
            <button 
              onClick={() => handleManualAction('water')}
              disabled={loading.water}
              className="w-full bg-slate-700 hover:bg-blue-500/20 border border-slate-600 hover:border-blue-500 text-white font-bold py-4 rounded-xl flex items-center justify-center transition-all group disabled:opacity-50"
            >
              <Droplet size={20} className="mr-2 text-blue-400 group-hover:animate-bounce" /> 
              {loading.water ? 'Refilling...' : 'Trigger Water Refill'}
            </button>
          </div>
        </Card>
      </div>
    </div>
  );
};


// --- MAIN APP LAYOUT ---

export default function App() {
  const [activeTab, setActiveTab] = useState('dashboard');
  const [isMobileMenuOpen, setIsMobileMenuOpen] = useState(false);

  const tabs = [
    { id: 'dashboard', label: 'Dashboard', icon: Home },
    { id: 'schedules', label: 'Schedules', icon: Calendar },
    { id: 'water', label: 'Water Logs', icon: Droplets },
    { id: 'alerts', label: 'Alerts', icon: Bell },
    { id: 'settings', label: 'Settings', icon: Settings },
  ];

  return (
    <div className="min-h-screen bg-slate-900 text-slate-200 font-sans selection:bg-cyan-500/30 flex flex-col md:flex-row">
      
      {/* Mobile Header */}
      <div className="md:hidden flex items-center justify-between p-4 bg-slate-800 border-b border-slate-700">
        <div className="flex items-center text-cyan-400 font-bold text-xl tracking-tight">
          <Activity className="mr-2" /> IoT Feeder
        </div>
        <button onClick={() => setIsMobileMenuOpen(!isMobileMenuOpen)} className="text-slate-400">
          {isMobileMenuOpen ? <X size={24} /> : <div className="space-y-1.5"><div className="w-6 h-0.5 bg-current"></div><div className="w-6 h-0.5 bg-current"></div><div className="w-6 h-0.5 bg-current"></div></div>}
        </button>
      </div>

      {/* Sidebar Navigation */}
      <aside className={`
        ${isMobileMenuOpen ? 'flex' : 'hidden'} md:flex flex-col 
        w-full md:w-64 bg-slate-800 border-r border-slate-700 z-40
        absolute md:relative min-h-screen transition-all shadow-2xl
      `}>
        <div className="hidden md:flex items-center text-cyan-400 font-bold text-xl tracking-tight p-6 border-b border-slate-700/50">
          <Activity className="mr-3" size={28} />
          <span>SmartFarm<span className="text-white">OS</span></span>
        </div>
        
        <nav className="flex-1 p-4 space-y-2">
          {tabs.map(tab => {
            const Icon = tab.icon;
            const isActive = activeTab === tab.id;
            return (
              <button
                key={tab.id}
                onClick={() => { setActiveTab(tab.id); setIsMobileMenuOpen(false); }}
                className={`w-full flex items-center px-4 py-3 rounded-xl transition-all font-medium text-sm
                  ${isActive 
                    ? 'bg-cyan-500/10 text-cyan-400 shadow-sm border border-cyan-500/20' 
                    : 'text-slate-400 hover:text-white hover:bg-slate-700/50'
                  }`}
              >
                <Icon size={18} className="mr-3" />
                {tab.label}
              </button>
            );
          })}
        </nav>

        <div className="p-4 border-t border-slate-700/50">
          <div className="flex items-center p-3 bg-slate-900/50 rounded-xl">
            <div className="w-8 h-8 rounded-full bg-gradient-to-tr from-cyan-500 to-emerald-400 flex items-center justify-center text-slate-900 font-bold">
              A
            </div>
            <div className="ml-3">
              <p className="text-sm font-medium text-white">Admin User</p>
              <p className="text-xs text-slate-400">ESP32 Controller</p>
            </div>
            <button className="ml-auto text-slate-500 hover:text-rose-400 transition-colors">
              <Power size={18} />
            </button>
          </div>
        </div>
      </aside>

      {/* Main Content Area */}
      <main className="flex-1 p-4 sm:p-6 lg:p-8 overflow-y-auto max-h-screen">
        <div className="max-w-6xl mx-auto pb-12">
          {activeTab === 'dashboard' && <DashboardTab />}
          {activeTab === 'schedules' && <ScheduleManager />}
          {activeTab === 'water' && <WaterLogs />}
          {activeTab === 'alerts' && <AlertsTab />}
          {activeTab === 'settings' && <SettingsTab />}
        </div>
      </main>
    </div>
  );
}