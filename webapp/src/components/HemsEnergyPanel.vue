<template>
    <CardElement
        v-for="kind in domains"
        :key="kind"
        :text="kind === 'tariff' ? 'Dynamic electricity tariff' : 'PV forecast'"
        textVariant="text-bg-primary"
        add-space
    >
        <p v-if="errors[kind]" class="alert alert-warning" role="alert">{{ errors[kind] }}</p>
        <p v-if="notices[kind]" class="alert alert-success" role="status">{{ notices[kind] }}</p>
        <template v-if="feeds[kind]">
            <dl class="row">
                <dt class="col-sm-4">Status</dt>
                <dd class="col-sm-8">{{ stateText(feeds[kind]!.state) }}</dd>
                <dt class="col-sm-4">Source</dt>
                <dd class="col-sm-8">
                    {{ feeds[kind]!.source || 'No data received' }} ·
                    {{
                        feeds[kind]!.provider === 'normalized_http' ? 'Local HTTP bridge' : 'Authenticated JSON upload'
                    }}
                </dd>
                <dt class="col-sm-4">Data freshness</dt>
                <dd class="col-sm-8">{{ feeds[kind]!.fresh ? 'Fresh' : 'Unavailable or stale' }}</dd>
                <dt class="col-sm-4">Last successful update</dt>
                <dd class="col-sm-8">{{ date(feeds[kind]!.fetched_at) }}</dd>
                <dt class="col-sm-4">Source timestamp</dt>
                <dd class="col-sm-8">{{ date(feeds[kind]!.generated_at) }}</dd>
                <dt class="col-sm-4">Future horizon</dt>
                <dd class="col-sm-8">
                    {{ feeds[kind]!.future_count }} intervals, until {{ date(feeds[kind]!.horizon_end) }} ·
                    {{
                        feeds[kind]!.horizon_complete
                            ? 'Requested horizon covered'
                            : 'Missing intervals or short horizon'
                    }}
                </dd>
                <dt class="col-sm-4">{{ kind === 'tariff' ? 'Current price' : 'Expected current power' }}</dt>
                <dd class="col-sm-8">{{ value(kind, feeds[kind]!.current) }}</dd>
                <template v-if="kind === 'tariff'">
                    <dt class="col-sm-4">Effective import price</dt>
                    <dd class="col-sm-8">
                        {{ price(feeds[kind]!.current?.import_price_micros_per_kwh, feeds[kind]!.currency) }}
                    </dd>
                </template>
            </dl>
            <p v-if="feeds[kind]!.last_error" class="alert alert-warning" role="status">
                Last update error: {{ stateText(feeds[kind]!.last_error!) }}. Previous data is retained for diagnostics.
            </p>
            <div v-if="feeds[kind]!.intervals.length" class="table-responsive">
                <table class="table table-sm">
                    <caption>
                        Next intervals. Times use your browser's time zone ({{
                            zone
                        }}).
                    </caption>
                    <thead>
                        <tr>
                            <th>From / until</th>
                            <th>{{ kind === 'tariff' ? 'Source price' : 'Power / energy' }}</th>
                            <th v-if="kind === 'tariff'">Import price</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="interval in feeds[kind]!.intervals" :key="interval.start">
                            <td>{{ date(interval.start) }} / {{ date(interval.end) }}</td>
                            <td>{{ value(kind, interval) }}</td>
                            <td v-if="kind === 'tariff'">
                                {{ price(interval.import_price_micros_per_kwh, feeds[kind]!.currency) }}
                            </td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <details v-if="drafts[kind]">
                <summary>Provider settings and JSON upload</summary>
                <form class="mt-3" @submit.prevent="save(kind)">
                    <div class="form-check mb-2">
                        <input
                            :id="`${kind}-enabled`"
                            v-model="drafts[kind]!.enabled"
                            type="checkbox"
                            class="form-check-input"
                        />
                        <label :for="`${kind}-enabled`" class="form-check-label"
                            >Enable {{ kind === 'tariff' ? 'tariff data' : 'PV forecast' }}</label
                        >
                    </div>
                    <label :for="`${kind}-url`" class="form-label"
                        >Local bridge URL (HTTP), or leave empty for JSON uploads</label
                    >
                    <input
                        :id="`${kind}-url`"
                        v-model="drafts[kind]!.url"
                        class="form-control mb-2"
                        maxlength="192"
                        placeholder="http://bridge.local/energy.json"
                    />
                    <div v-for="field in settingFields" :key="field.key" class="mb-2">
                        <label :for="`${kind}-${field.key}`" class="form-label">{{ field.label }} (seconds)</label>
                        <input
                            :id="`${kind}-${field.key}`"
                            v-model.number="drafts[kind]![field.key]"
                            type="number"
                            class="form-control"
                            :min="field.min"
                            :max="field.max"
                            step="1"
                            required
                        />
                    </div>
                    <button class="btn btn-primary me-2" :disabled="busy[kind]">Save provider settings</button>
                    <button
                        type="button"
                        class="btn btn-secondary"
                        :disabled="busy[kind] || !feeds[kind]!.enabled || feeds[kind]!.provider !== 'normalized_http'"
                        @click="refresh(kind)"
                    >
                        Refresh now
                    </button>
                </form>
                <label :for="`${kind}-upload`" class="form-label mt-3"
                    >Upload normalized JSON (maximum 128 intervals / 20 kB)</label
                >
                <input
                    :id="`${kind}-upload`"
                    class="form-control"
                    type="file"
                    accept="application/json,.json"
                    :disabled="busy[kind] || !feeds[kind]!.enabled || feeds[kind]!.provider !== 'normalized_push'"
                    @change="upload(kind, $event)"
                />
                <p class="form-text">
                    Both feeds are optional. A local bridge can supply normalized data from your chosen provider.
                    Missing data is never filled with invented values.
                </p>
                <a
                    href="https://github.com/dawid-woitaschek/OpenDTU-HEMS/blob/feature/hems-real-control/docs/HEMS_Energy_Data.md"
                    target="_blank"
                    rel="noopener"
                    >JSON format and bridge API</a
                >
            </details>
        </template>
    </CardElement>
</template>
<script setup lang="ts">
import { getCurrentInstance, onMounted, onUnmounted, reactive } from 'vue';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
type Kind = 'tariff' | 'forecast';
type Settings = {
    enabled: boolean;
    url: string;
    refresh_seconds: number;
    max_age_seconds: number;
    horizon_seconds: number;
};
type Interval = {
    start: number;
    end: number;
    price_micros_per_kwh?: number;
    import_price_micros_per_kwh?: number | null;
    power_w?: number;
    energy_wh?: number;
};
type Feed = {
    config: Settings;
    provider: string;
    source: string;
    state: string;
    fresh: boolean;
    enabled: boolean;
    fetched_at: number | null;
    generated_at: number | null;
    horizon_end: number | null;
    future_count: number;
    horizon_complete: boolean;
    last_error: string | null;
    currency?: string;
    current: Interval | null;
    intervals: Interval[];
};
const domains: Kind[] = ['tariff', 'forecast'];
const feeds = reactive<Partial<Record<Kind, Feed>>>({});
const drafts = reactive<Partial<Record<Kind, Settings>>>({});
const errors = reactive({ tariff: '', forecast: '' });
const notices = reactive({ tariff: '', forecast: '' });
const busy = reactive({ tariff: false, forecast: false });
const settingFields = [
    { key: 'refresh_seconds', label: 'HTTP refresh interval', min: 60, max: 86400 },
    { key: 'max_age_seconds', label: 'Maximum data age', min: 60, max: 604800 },
    { key: 'horizon_seconds', label: 'Requested future horizon', min: 900, max: 604800 },
] as const;
const zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
let timer: ReturnType<typeof setInterval> | undefined;
let disposed = false;
const date = (utc: number | null) =>
    utc ? new Date(utc * 1000).toLocaleString(undefined, { timeZoneName: 'short' }) : 'No data';
function price(micros?: number | null, currency?: string) {
    return micros == null
        ? 'Unknown'
        : `${(micros / 1000000).toLocaleString(undefined, { maximumFractionDigits: 6 })} ${currency || ''}/kWh`;
}
function value(kind: Kind, interval?: Interval | null) {
    if (!interval) return 'No interval available';
    return kind === 'tariff'
        ? price(interval.price_micros_per_kwh, feeds[kind]?.currency)
        : `${interval.power_w} W / ${interval.energy_wh?.toLocaleString(undefined, { maximumFractionDigits: 1 })} Wh`;
}
function stateText(state: string) {
    const labels: Record<string, string> = {
        disabled: 'Disabled',
        waiting_for_time: 'Waiting for clock synchronization',
        unavailable: 'Waiting for data',
        ready: 'Ready',
        partial: 'Partial future data',
        stale: 'Stale data',
        error: 'Update failed',
        network_unavailable: 'Network unavailable',
        http_unavailable: 'HTTP bridge unavailable',
        malformed_or_oversized_json: 'Malformed or oversized JSON',
        older_source_timestamp: 'Source data older than previous update',
        clock_or_source_time_invalid: 'Invalid clock or future source timestamp',
    };
    return labels[state] || state.replaceAll('_', ' ');
}
function message(error: unknown) {
    return error && typeof error === 'object' && 'message' in error
        ? String(error.message)
        : String(error || 'Request failed');
}
async function api(kind: Kind, suffix = '', data?: unknown) {
    const body = new FormData();
    if (data !== undefined) body.append('data', JSON.stringify(data));
    return fetch(`/api/hems/energy/${kind}${suffix}`, {
        headers: authHeader(),
        ...(data !== undefined ? { method: 'POST', body } : {}),
    }).then((response) => handleResponse(response, emitter, router, true));
}
async function load(kind: Kind, reset = false) {
    try {
        const feed = (await api(kind)) as Feed;
        if (disposed) return;
        feeds[kind] = feed;
        if (!drafts[kind] || reset) drafts[kind] = { ...feed.config };
        errors[kind] = '';
    } catch (error) {
        errors[kind] = `Could not reload provider status: ${message(error)}`;
    }
}
async function perform(kind: Kind, suffix: string, data: unknown, success: string, reset = false) {
    busy[kind] = true;
    errors[kind] = '';
    notices[kind] = '';
    try {
        await api(kind, suffix, data);
        notices[kind] = success;
        await load(kind, reset);
    } catch (error) {
        errors[kind] = message(error);
    } finally {
        busy[kind] = false;
    }
}
function save(kind: Kind) {
    return perform(kind, '/config', drafts[kind], 'Provider settings saved.', true);
}
function refresh(kind: Kind) {
    return perform(kind, '/refresh', {}, 'Refresh queued. Status updates automatically.');
}
async function upload(kind: Kind, event: Event) {
    const input = event.target as HTMLInputElement,
        file = input.files?.[0];
    if (!file) return;
    try {
        if (file.size > 20000) throw new Error('JSON file exceeds 20 kB.');
        await perform(kind, '', JSON.parse(await file.text()), 'Normalized data accepted.');
    } catch (error) {
        errors[kind] = message(error);
    }
    input.value = '';
}
onMounted(() => {
    domains.forEach((kind) => load(kind));
    timer = setInterval(
        () =>
            domains.forEach((kind) => {
                if (!busy[kind]) void load(kind);
            }),
        15000
    );
});
onUnmounted(() => {
    disposed = true;
    if (timer) clearInterval(timer);
});
</script>
