// Canonical destinations shared by the sidebar and energy-management tabs.
export const hemsNavigation = [
    { path: '/energy/planning', label: 'settings' },
    { path: '/energy/integrations', label: 'integrations' },
    { path: '/energy/diagnostics', label: 'diagnostics' },
] as const;

export const mainNavigation = [
    {
        label: 'start',
        items: [
            { path: '/', label: 'overview' },
            { path: '/live', label: 'live' },
        ],
    },
    {
        label: 'plant',
        items: [
            { path: '/plant/solar', label: 'solar' },
            { path: '/plant/inverters', label: 'inverters' },
            { path: '/plant/battery', label: 'battery' },
            { path: '/plant/wallbox', label: 'wallbox' },
            { path: '/plant/power-meter', label: 'powerMeter' },
        ],
    },
    {
        label: 'energy',
        items: [
            { path: '/energy/planning', label: 'planning' },
            { path: '/energy/integrations', label: 'integrations' },
            { path: '/energy/diagnostics', label: 'diagnostics' },
        ],
    },
    {
        label: 'system',
        items: [
            { path: '/settings/network', label: 'network' },
            { path: '/settings/mqtt', label: 'mqtt' },
            { path: '/settings/ntp', label: 'time' },
            { path: '/settings/dtu', label: 'hardware' },
            { path: '/settings/security', label: 'security' },
            { path: '/settings/logging', label: 'logging' },
            { path: '/settings/config', label: 'configuration' },
            { path: '/firmware/upgrade', label: 'firmware' },
            { path: '/info/system', label: 'systemStatus' },
            { path: '/maintenance/reboot', label: 'reboot' },
        ],
    },
] as const;
