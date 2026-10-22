export type EnergyManagement = {
    enabled: boolean;
    preference: number;
    pv_export: boolean;
    battery_export: boolean;
    ev_protection: boolean;
    max_export_w: number;
    pv_ac_limit_w: number;
    automatic_load: boolean;
    wear_source: number;
    degradation_micros: number;
    buffer_micros: number;
    efc_capacity_wh: number;
    terminal_hours: number;
    terminal_price_configured: boolean;
    terminal_price_micros: number;
    hysteresis_micros: number;
};
export const defaultEnergyManagement = (): EnergyManagement => ({
    enabled: false,
    preference: 0,
    pv_export: true,
    battery_export: false,
    ev_protection: true,
    max_export_w: 0,
    pv_ac_limit_w: 0,
    automatic_load: false,
    wear_source: 0,
    degradation_micros: 0,
    buffer_micros: 0,
    efc_capacity_wh: 0,
    terminal_hours: 12,
    terminal_price_configured: false,
    terminal_price_micros: 0,
    hysteresis_micros: 0,
});
// A preset describes goals; it never grants control or invents device ratings.
export function referencePreset(current: EnergyManagement): EnergyManagement {
    return {
        ...current,
        preference: 1,
        pv_export: true,
        battery_export: false,
        ev_protection: true,
        automatic_load: true,
    };
}
