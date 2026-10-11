export interface HemsConfig {
    version: number;
    enabled: boolean;
    warp: {
        enabled: boolean;
        host: string;
        port: number;
    };
}
