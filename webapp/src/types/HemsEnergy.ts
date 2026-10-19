export interface PlaneStatus {
    index?: number;
    name?: string;
    enabled?: boolean;
    state?: string;
    attempted_at?: number;
    fetched_at: number;
    last_error: string | null;
    http_status?: number;
    request?: { dns?: number; tls_error?: number; tls_error_text?: string; http_error?: string };
}
export type EnergyKind = 'tariff' | 'forecast';
export type EnergyView = 'overview' | 'settings' | 'diagnostics';
export interface PvPlane {
    enabled: boolean;
    name: string;
    latitude_e6: number | null;
    longitude_e6: number | null;
    tilt: number;
    azimuth: number;
    peak_w: number;
}
export interface EnergySettings {
    provider: string;
    planes: PvPlane[];
    pricing: {
        enabled: boolean;
        vat_basis_points: number;
        grid_micros_per_kwh: number;
        markup_micros_per_kwh: number;
        monthly_micros: number;
    };
    enabled: boolean;
    url: string;
    refresh_seconds: number;
    max_age_seconds: number;
    horizon_seconds: number;
}
export interface EnergyInterval {
    start: number;
    end: number;
    price_micros_per_kwh?: number;
    import_price_micros_per_kwh?: number | null;
    power_w?: number;
    energy_wh?: number;
}
export interface EnergyFeed {
    manual_refresh?: {
        allowed: boolean;
        reason: string | null;
        retry_after_seconds: number;
        next_allowed?: number | null;
    };
    revision: number;
    config: EnergySettings;
    next_refresh: number;
    source_age_seconds: number | null;
    source_fresh?: boolean | null;
    transport_fresh?: boolean;
    transport_age_seconds: number;
    rate_remaining: number;
    attribution?: string;
    plane_status?: PlaneStatus[];
    provider: string;
    source: string;
    state: string;
    fresh: boolean;
    valid?: boolean;
    current_covered?: boolean;
    planning_usable?: boolean;
    partial_horizon?: boolean;
    enabled: boolean;
    fetched_at: number | null;
    generated_at: number | null;
    horizon_end: number | null;
    future_count: number;
    horizon_complete: boolean;
    last_error: string | null;
    currency?: string;
    current: EnergyInterval | null;
    intervals: EnergyInterval[];
}
