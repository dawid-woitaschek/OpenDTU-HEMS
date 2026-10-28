import HemsAdminView from '@/views/HemsAdminView.vue';
import HemsDevicesView from '@/views/HemsDevicesView.vue';
import HemsDiagnosticsView from '@/views/HemsDiagnosticsView.vue';
import HemsInfoView from '@/views/HemsInfoView.vue';
import AboutView from '@/views/AboutView.vue';
import ConfigAdminView from '@/views/ConfigAdminView.vue';
import ConsoleInfoView from '@/views/ConsoleInfoView.vue';
import DeviceAdminView from '@/views/DeviceAdminView.vue';
import DtuAdminView from '@/views/DtuAdminView.vue';
import ErrorView from '@/views/ErrorView.vue';
import FirmwareUpgradeView from '@/views/FirmwareUpgradeView.vue';
import LiveDataView from '@/views/LiveDataView.vue';
import PlantDeviceView from '@/views/PlantDeviceView.vue';
import IntegrationsView from '@/views/IntegrationsView.vue';
import PowerLimiterAdminView from '@/views/PowerLimiterAdminView.vue';
import InverterAdminView from '@/views/InverterAdminView.vue';
import LoginView from '@/views/LoginView.vue';
import MaintenanceRebootView from '@/views/MaintenanceRebootView.vue';
import LoggingAdminView from '@/views/LoggingAdminView.vue';
import MqttAdminView from '@/views/MqttAdminView.vue';
import MqttInfoView from '@/views/MqttInfoView.vue';
import NetworkAdminView from '@/views/NetworkAdminView.vue';
import NetworkInfoView from '@/views/NetworkInfoView.vue';
import NtpAdminView from '@/views/NtpAdminView.vue';
import NtpInfoView from '@/views/NtpInfoView.vue';
import SecurityAdminView from '@/views/SecurityAdminView.vue';
import SystemInfoView from '@/views/SystemInfoView.vue';
import WaitRestartView from '@/views/WaitRestartView.vue';
import { createRouter, createWebHistory } from 'vue-router';

const router = createRouter({
    history: createWebHistory(import.meta.env.BASE_URL),
    linkActiveClass: 'active',
    scrollBehavior() {
        return new Promise((resolve) => {
            setTimeout(() => {
                resolve({ top: 0 });
            }, 100);
        });
    },
    routes: [
        {
            path: '/energy/diagnostics',
            alias: '/hems/diagnostics',
            name: 'HEMS Diagnostics',
            component: HemsDiagnosticsView,
        },
        {
            path: '/energy/planning',
            alias: ['/settings/hems', '/hems/settings'],
            name: 'HEMS Settings',
            component: HemsAdminView,
        },
        { path: '/hems', redirect: '/' },
        { path: '/info/hems', redirect: '/' },
        { path: '/hems/devices', redirect: '/energy/integrations' },
        { path: '/hems/profiles', redirect: '/energy/integrations' },
        { path: '/energy/integrations', name: 'Integrations', component: IntegrationsView },
        { path: '/', name: 'Home', component: HemsInfoView },
        { path: '/live', name: 'Live Data', component: LiveDataView },
        { path: '/plant/inverters', name: 'Inverters', component: LiveDataView, props: { invertersOnly: true } },
        { path: '/plant/solar', name: 'Solar', component: PlantDeviceView, props: { device: 'solar' } },
        { path: '/plant/battery', name: 'Battery', component: PlantDeviceView, props: { device: 'battery' } },
        { path: '/plant/wallbox', name: 'Wallbox', component: HemsDevicesView, props: { device: 'wallbox' } },
        {
            path: '/plant/power-meter',
            name: 'Power Meter',
            component: PlantDeviceView,
            props: { device: 'powerMeter' },
        },
        {
            path: '/login',
            name: 'Login',
            component: LoginView,
        },
        {
            path: '/error?status=:status&message=:message',
            name: 'Error',
            component: ErrorView,
        },
        {
            path: '/about',
            name: 'About',
            component: AboutView,
        },
        {
            path: '/info/network',
            name: 'Network',
            component: NetworkInfoView,
        },
        {
            path: '/info/system',
            name: 'System',
            component: SystemInfoView,
        },
        {
            path: '/info/ntp',
            name: 'NTP',
            component: NtpInfoView,
        },
        {
            path: '/info/mqtt',
            name: 'MqTT',
            component: MqttInfoView,
        },
        {
            path: '/info/console',
            name: 'Web Console',
            component: ConsoleInfoView,
        },
        {
            path: '/settings/network',
            name: 'Network Settings',
            component: NetworkAdminView,
        },
        {
            path: '/settings/ntp',
            name: 'NTP Settings',
            component: NtpAdminView,
        },
        { path: '/settings/solarcharger', redirect: '/plant/solar' },
        { path: '/settings/powermeter', redirect: '/plant/power-meter' },
        {
            path: '/plant/inverters/power-limit',
            alias: '/settings/powerlimiter',
            name: 'Power limiter Settings',
            component: PowerLimiterAdminView,
        },
        { path: '/settings/battery', redirect: '/plant/battery' },
        { path: '/settings/chargerac', redirect: '/plant/battery' },
        {
            path: '/settings/mqtt',
            name: 'MqTT Settings',
            component: MqttAdminView,
        },
        {
            path: '/plant/inverters/configuration',
            alias: '/settings/inverter',
            name: 'Inverter Settings',
            component: InverterAdminView,
        },
        {
            path: '/settings/dtu',
            name: 'DTU Settings',
            component: DtuAdminView,
        },
        {
            path: '/settings/device',
            name: 'Device Manager',
            component: DeviceAdminView,
        },
        {
            path: '/firmware/upgrade',
            name: 'Firmware Upgrade',
            component: FirmwareUpgradeView,
        },
        {
            path: '/settings/config',
            name: 'Config Management',
            component: ConfigAdminView,
        },
        {
            path: '/settings/security',
            name: 'Security',
            component: SecurityAdminView,
        },
        {
            path: '/settings/logging',
            name: 'Logging',
            component: LoggingAdminView,
        },
        {
            path: '/maintenance/reboot',
            name: 'Device Reboot',
            component: MaintenanceRebootView,
        },
        {
            path: '/wait',
            name: 'Wait Restart',
            component: WaitRestartView,
        },
    ],
});

export default router;
