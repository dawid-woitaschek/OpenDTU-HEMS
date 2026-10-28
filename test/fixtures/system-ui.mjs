// Existing settings/status envelopes used only for the route/layout audit.
export function systemUiResponse(path) {
    switch (path) {
        case '/api/network/config': return { ssid: 'fixture', password: '', hostname: 'opendtu', dhcp: true, aptimeout: 180, mdnsenabled: true };
        case '/api/network/status': return { sta_status: true, sta_ssid: 'fixture', sta_rssi: -50, ap_status: false, network_hostname: 'opendtu', network_ip: '192.0.2.10', network_mode: 'Wi-Fi' };
        case '/api/mqtt/config': return { mqtt_enabled: false };
        case '/api/mqtt/status': return { mqtt_enabled: false, mqtt_connected: false };
        case '/api/ntp/zones': return [{ name: 'Europe/Berlin', value: 'CET-1CEST,M3.5.0,M10.5.0/3' }];
        case '/api/ntp/config': return { ntp_server: 'pool.ntp.org', ntp_timezone_descr: 'Europe/Berlin', latitude: 52, longitude: 13, sunsettype: 0 };
        case '/api/ntp/time': return { epoch: 1792924200 };
        case '/api/ntp/status': return { ntp_server: 'pool.ntp.org', ntp_timezone_descr: 'Europe/Berlin', ntp_status: true, ntp_localtime: '2026-10-25 13:30:00', sun_isDayPeriod: true, sun_isSunsetAvailable: false };
        case '/api/dtu/config': return { serial: '12345678', pollinterval: 5, nrf_enabled: false, cmt_enabled: false, country_def: [] };
        case '/api/security/config': return { password: '', allow_readonly: true };
        case '/api/logging/config': return { loglevel: { default: 3, modules: [] } };
        case '/api/inverter/list': return { inverter: [] };
        case '/api/powerlimiter/metadata': return { inverters: [], power_meter_enabled: true, battery_enabled: false, charge_controller_enabled: false };
        case '/api/powerlimiter/config': return { enabled: false, inverters: [] };
        case '/api/file/list': return { files: [] };
        case '/api/device/config': return { curPin: { name: 'Default' }, display: { locale: 'fr', rotation: 0, contrast: 50, diagrammode: 0, diagramduration: 24, power_safe: false, screensaver: false }, led: [{ brightness: 50 }, { brightness: 50 }] };
        case '/api/file/get': return [];
        case '/api/solarchargerlivedata/status': return { dpl: { PLSTATE: 2, PLLIMIT: 1000 }, solarcharger: { full_update: true, instances: {} } };
        case '/api/system/status': return { reset_diagnostics: {}, task_details: [], uarts: [], heap_total: 200000, heap_used: 50000, heap_min_free: 100000, heap_max_block: 80000, psram_total: 0, uarts_applied: [], sdk_version: 'fixture', git_hash: undefined };
        case '/api/firmware/status': return { git_sha: '1'.repeat(40), pioenv: 'generic_esp32s3_usb', ota_supported: true };
        default: return undefined;
    }
}
