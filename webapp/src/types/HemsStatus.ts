export interface HemsStatus {
    state: 'Disabled' | 'Monitor';
    decision: 'None';
    external_devices: number;
}
