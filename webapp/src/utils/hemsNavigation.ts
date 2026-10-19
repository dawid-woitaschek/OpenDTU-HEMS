// Shared destinations keep the global menu and local navigation consistent.
export const hemsNavigation = [
    { path: '/hems', label: 'overview' },
    { path: '/hems/settings', label: 'settings' },
    { path: '/hems/devices', label: 'devices' },
    { path: '/hems/diagnostics', label: 'diagnostics' },
] as const;
