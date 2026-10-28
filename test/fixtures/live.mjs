// Hardware-free live snapshot; fixtures never enter the production bundle.
export function createLiveFixture() {
    const value = (v, u = 'W') => ({ v, u, d: 1, max: v });
    return {
        inverters: ['East', 'West'].map((name, index) => ({ name, serial: String(100000000001 + index), order: index, data_age: 1, data_age_ms: 1000, poll_enabled: true, reachable: true, producing: true, limit_relative: 75 - index * 25, limit_absolute: 600 - index * 200, events: index,
            AC: [{ Power: value(400), Voltage: value(230, 'V') }], DC: [], INV: [{ Temperature: value(38, '°C') }], radio_stats: { tx_request: 10, tx_re_request: 0, rx_success: 10, rx_fail_nothing: 0, rx_fail_partial: 0, rx_fail_corrupt: 0, rssi: -60 } })),
        total: { Power: value(800), YieldDay: value(4, 'kWh'), YieldTotal: value(200, 'kWh') },
        hints: { time_sync: false, default_password: false, radio_problem: false, pin_mapping_issue: false },
        solarcharger: { enabled: false }, battery: { enabled: false }, gridcharger: { enabled: false }, power_meter: { enabled: true, Power: value(-245) },
    };
}
