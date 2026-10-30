export interface NavigationItem {
    path: string;
    label: string;
    children?: NavigationItem[];
}
export const mainNavigation: { label: string; items: NavigationItem[] }[] = [
    {
        label: 'start',
        items: [{ path: '/', label: 'overview' }],
    },
    {
        label: 'plant',
        items: [
            {
                path: '/plant/inverters',
                label: 'inverters',
                children: [
                    { path: '/plant/inverters', label: 'overview' },
                    { path: '/plant/inverters/live', label: 'live' },
                    { path: '/plant/inverters/configuration', label: 'configuration' },
                ],
            },
            { path: '/plant/solar-charger', label: 'solarCharger' },
            { path: '/plant/battery', label: 'battery' },
            { path: '/plant/wallbox', label: 'wallbox' },
            { path: '/plant/power-meter', label: 'powerMeter' },
        ],
    },
    {
        label: 'energy',
        items: [
            { path: '/energy/planning', label: 'planning' },
            { path: '/energy/power-control', label: 'powerControl' },
            { path: '/energy/integrations', label: 'integrations' },
            { path: '/energy/diagnostics', label: 'diagnostics' },
        ],
    },
    {
        label: 'system',
        items: [
            {
                path: '/settings/network',
                label: 'network',
                children: [
                    { path: '/settings/network', label: 'configuration' },
                    { path: '/info/network', label: 'status' },
                ],
            },
            {
                path: '/settings/mqtt',
                label: 'mqtt',
                children: [
                    { path: '/settings/mqtt', label: 'configuration' },
                    { path: '/info/mqtt', label: 'status' },
                ],
            },
            {
                path: '/settings/ntp',
                label: 'time',
                children: [
                    { path: '/settings/ntp', label: 'configuration' },
                    { path: '/info/ntp', label: 'status' },
                ],
            },
            {
                path: '/settings/dtu',
                label: 'hardware',
                children: [
                    { path: '/settings/dtu', label: 'configuration' },
                    { path: '/settings/device', label: 'deviceManager' },
                ],
            },
            { path: '/settings/security', label: 'security' },
            {
                path: '/settings/logging',
                label: 'logging',
                children: [
                    { path: '/settings/logging', label: 'configuration' },
                    { path: '/info/console', label: 'console' },
                ],
            },
            { path: '/settings/config', label: 'configuration' },
            { path: '/firmware/upgrade', label: 'firmware' },
            { path: '/info/system', label: 'systemStatus' },
            { path: '/system/remote-diagnostics', label: 'remoteDiagnostics' },
            { path: '/maintenance/reboot', label: 'reboot' },
        ],
    },
];
