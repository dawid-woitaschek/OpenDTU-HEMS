// WebSocketService.ts

export interface WebSocketCallbacks {
    onMessage?: (event: MessageEvent) => void;
    onOpen?: (event: Event) => void;
    onClose?: (event: CloseEvent) => void;
    onError?: (event: Event) => void;
}

export default class WebSocketService {
    private url: string;
    private socket: WebSocket | null = null;

    private reconnectDelay = 200;
    private maxDelay = 3000;

    private reconnectTimer: number | null = null;
    private connectTimer: number | null = null;
    private stopped = true;

    private heartbeatInterval: number | null = null;
    private heartbeatTimeout = 25000; // 25s ping interval

    private onMessage: (event: MessageEvent) => void;
    private onOpen: (event: Event) => void;
    private onClose: (event: CloseEvent) => void;
    private onError: (event: Event) => void;

    constructor(url: string, callbacks: WebSocketCallbacks = {}) {
        this.url = url;
        this.onMessage = callbacks.onMessage ?? (() => {});
        this.onOpen = callbacks.onOpen ?? (() => {});
        this.onClose = callbacks.onClose ?? (() => {});
        this.onError = callbacks.onError ?? (() => {});
    }

    /** Public connect entry */
    public connect(): void {
        this.stopped = false;

        if (this.socket?.readyState === WebSocket.CONNECTING || this.socket?.readyState === WebSocket.OPEN) {
            return;
        }

        this.clearReconnectTimer();

        const isiOS = /iPad|iPhone|iPod/.test(navigator.userAgent);
        if (isiOS) {
            // Fix for WebKit timing bug on all iOS browsers (Safari/Chrome/Vivaldi/etc.).
            // Keep the timer cancellable so leaving a route cannot create a socket afterwards.
            this.clearConnectTimer();
            this.connectTimer = window.setTimeout(() => {
                this.connectTimer = null;
                if (!this.stopped) {
                    this.openSocket();
                }
            }, 50);
        } else {
            this.openSocket();
        }
    }

    /** Internal open function */
    private openSocket(): void {
        if (this.stopped) {
            return;
        }
        if (this.socket?.readyState === WebSocket.CONNECTING || this.socket?.readyState === WebSocket.OPEN) {
            return;
        }

        try {
            const socket = new WebSocket(this.url);
            this.socket = socket;

            socket.onopen = (event: Event) => {
                if (this.stopped || this.socket !== socket) {
                    socket.close();
                    return;
                }

                this.reconnectDelay = 200; // reset backoff
                this.startHeartbeat();
                this.onOpen(event);
            };

            socket.onmessage = (event: MessageEvent) => {
                if (!this.stopped && this.socket === socket) {
                    this.onMessage(event);
                }
            };

            socket.onclose = (event: CloseEvent) => {
                if (this.socket === socket) {
                    this.socket = null;
                }
                this.stopHeartbeat();

                if (this.stopped) {
                    return;
                }

                this.onClose(event);
                this.scheduleReconnect();
            };

            socket.onerror = (event: Event) => {
                if (!this.stopped && this.socket === socket) {
                    this.onError(event);
                }
                // Closing triggers the normal reconnect path exactly once.
                socket.close();
            };
        } catch {
            this.socket = null;
            this.scheduleReconnect();
        }
    }

    /** Reconnect with gradual backoff */
    private scheduleReconnect(): void {
        this.stopHeartbeat();
        if (this.stopped || this.reconnectTimer !== null) {
            return;
        }

        this.reconnectTimer = window.setTimeout(() => {
            this.reconnectTimer = null;
            if (this.stopped) {
                return;
            }

            this.reconnectDelay = Math.min(this.reconnectDelay * 1.4, this.maxDelay);
            this.connect();
        }, this.reconnectDelay);
    }

    /** Heartbeat keeps NAT/websocket alive */
    private startHeartbeat(): void {
        this.stopHeartbeat();

        this.heartbeatInterval = window.setInterval(() => {
            if (!this.stopped && this.socket?.readyState === WebSocket.OPEN) {
                this.socket.send(JSON.stringify({ type: 'ping' }));
            }
        }, this.heartbeatTimeout);
    }

    private stopHeartbeat(): void {
        if (this.heartbeatInterval !== null) {
            clearInterval(this.heartbeatInterval);
            this.heartbeatInterval = null;
        }
    }

    private clearReconnectTimer(): void {
        if (this.reconnectTimer !== null) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
    }

    private clearConnectTimer(): void {
        if (this.connectTimer !== null) {
            clearTimeout(this.connectTimer);
            this.connectTimer = null;
        }
    }

    /** Explicit close function */
    public close(): void {
        this.stopped = true;
        this.stopHeartbeat();
        this.clearReconnectTimer();
        this.clearConnectTimer();

        const socket = this.socket;
        this.socket = null;

        if (socket) {
            socket.onopen = null;
            socket.onmessage = null;
            socket.onclose = null;
            socket.onerror = null;

            if (socket.readyState !== WebSocket.CLOSING && socket.readyState !== WebSocket.CLOSED) {
                socket.close();
            }
        }
    }
}
