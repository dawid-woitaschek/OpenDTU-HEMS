// API and persisted values remain integer micro EUR and VAT basis points.
export const priceFields = [
    {
        key: 'grid_micros_per_kwh',
        label: 'Variable grid fees and taxes',
        unit: 'ct/kWh',
        scale: 10000,
        min: -214748.3648,
        max: 214748.3647,
        step: 0.0001,
    },
    {
        key: 'markup_micros_per_kwh',
        label: 'Supplier markup',
        unit: 'ct/kWh',
        scale: 10000,
        min: -214748.3648,
        max: 214748.3647,
        step: 0.0001,
    },
    {
        key: 'monthly_micros',
        label: 'Fixed monthly base fee',
        unit: 'EUR/month',
        scale: 1000000,
        min: 0,
        max: 4294.967295,
        step: 0.000001,
    },
] as const;
export function toDisplay(value: number, scale: number) {
    return value / scale;
}
export function toCanonical(value: string | number, scale: number) {
    return Math.round(Number(value) * scale);
}
