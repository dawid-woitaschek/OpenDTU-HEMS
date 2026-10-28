import { computed, readonly, ref, shallowRef } from 'vue';
import type { Emitter, EventType } from 'mitt';
import type { Router } from 'vue-router';
import type { Inverter, LiveData } from '@/types/LiveDataStatus';
import { authHeader, authUrl, handleResponse } from './authentication';
import WebSocketService from './websocketService';

// App owns one /livedata connection. Pages consume snapshots; navigation never opens a socket.
const data = shallowRef<LiveData>();
const connection = ref<'connecting' | 'connected' | 'disconnected'>('connecting');
const unavailable = ref(false);
const loading = ref(true);
const now = ref(Date.now());
const received = new Map<string, number>();
const aggregates = ['total', 'hints', 'solarcharger', 'battery', 'gridcharger', 'power_meter'] as const;
const inverterFrames = new Map<string, number>();
const aggregateFrames = new Map<(typeof aggregates)[number], number>();
let frameRevision = 0;
let socket: WebSocketService | undefined;
let timer: ReturnType<typeof setInterval> | undefined;
let request: Promise<void> | undefined;
let controller: AbortController | undefined;
let refresh: () => Promise<void> = async () => {};

const inverters = computed<Inverter[]>(() =>
    (data.value?.inverters || [])
        .map((inv) => {
            const age =
                Math.max(0, inv.data_age_ms ?? inv.data_age * 1000) +
                Math.max(0, now.value - (received.get(inv.serial) ?? now.value));
            return { ...inv, data_age_ms: age, data_age: Math.floor(age / 1000) };
        })
        .sort((a, b) => a.order - b.order)
);

function accept(snapshot: Partial<LiveData>, full = false, requestedAt = frameRevision) {
    if (!data.value && !full) return;
    const next = { ...data.value, ...snapshot } as LiveData;
    const retained = new Set<string>();
    if (full && data.value) {
        // A refresh discovers configuration changes, but cannot roll back frames
        // received while its HTTP request was in flight.
        const current = new Map(data.value.inverters?.map((inv) => [inv.serial, inv]));
        next.inverters = (snapshot.inverters || []).map((inv) => {
            if ((inverterFrames.get(inv.serial) || 0) > requestedAt && current.has(inv.serial)) {
                retained.add(inv.serial);
                return current.get(inv.serial)!;
            }
            return inv;
        });
        for (const key of aggregates) {
            if ((aggregateFrames.get(key) || 0) > requestedAt) Object.assign(next, { [key]: data.value[key] });
        }
    }
    if (!full && data.value) {
        frameRevision++;
        for (const key of aggregates) {
            Object.assign(next, { [key]: { ...data.value[key], ...snapshot[key] } });
            if (snapshot[key] !== undefined) aggregateFrames.set(key, frameRevision);
        }
    }
    if (!full && snapshot.inverters) {
        const merged = new Map(data.value?.inverters.map((inv) => [inv.serial, inv]));
        for (const inv of snapshot.inverters) merged.set(inv.serial, inv);
        next.inverters = [...merged.values()];
    }
    for (const inv of snapshot.inverters || []) {
        if (!retained.has(inv.serial)) received.set(inv.serial, Date.now());
        if (!full) inverterFrames.set(inv.serial, frameRevision);
    }
    if (full) {
        const configured = new Set(next.inverters?.map((inv) => inv.serial));
        for (const serial of received.keys()) {
            if (!configured.has(serial)) {
                received.delete(serial);
                inverterFrames.delete(serial);
            }
        }
    }
    data.value = next;
    unavailable.value = false;
    loading.value = false;
    now.value = Date.now();
}

export function startLiveData(emitter: Emitter<Record<EventType, unknown>>, router: Router) {
    stopLiveData();
    connection.value = 'connecting';
    const owner = new AbortController();
    controller = owner;
    refresh = () => {
        if (request) return request;
        const requestedAt = frameRevision;
        request = (async () => {
            try {
                const response = await fetch('/api/livedata/status', { headers: authHeader(), signal: owner.signal });
                const snapshot = await handleResponse(response, emitter, router);
                if (!owner.signal.aborted) accept(snapshot, true, requestedAt);
            } catch {
                if (!owner.signal.aborted) {
                    unavailable.value = true;
                    loading.value = false;
                }
            } finally {
                if (controller === owner) request = undefined;
            }
        })();
        return request;
    };
    const { protocol, host } = location;
    socket = new WebSocketService(`${protocol === 'https:' ? 'wss' : 'ws'}://${authUrl()}${host}/livedata`, {
        onOpen() {
            connection.value = 'connected';
            void refresh();
        },
        onClose() {
            connection.value = 'disconnected';
        },
        onError() {
            connection.value = 'disconnected';
        },
        onMessage(event) {
            try {
                const snapshot = JSON.parse(event.data);
                if (snapshot && typeof snapshot === 'object' && !('type' in snapshot)) accept(snapshot);
            } catch {
                unavailable.value = true;
            }
        },
    });
    void refresh();
    socket.connect();
    timer = setInterval(() => {
        now.value = Date.now();
    }, 1000);
}

export function stopLiveData() {
    controller?.abort();
    socket?.close();
    socket = undefined;
    if (timer) clearInterval(timer);
    request = undefined;
}

export function useLiveData() {
    return {
        data: readonly(data),
        inverters,
        connection: readonly(connection),
        unavailable: readonly(unavailable),
        loading: readonly(loading),
        reload: () => refresh(),
    };
}
