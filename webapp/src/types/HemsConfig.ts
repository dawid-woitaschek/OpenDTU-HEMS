export type HemsDeviceControlMode = 'monitor' | 'hems';

export interface HemsConfig {
    version: number;
    enabled: boolean;
    warp: {
        enabled: boolean;
        host: string;
        port: number;
    };
    storage: {
        enabled: boolean;
        host: string;
        profile: string;
        port: number;
        unit: number;
        control_mode: HemsDeviceControlMode;
    };
}
