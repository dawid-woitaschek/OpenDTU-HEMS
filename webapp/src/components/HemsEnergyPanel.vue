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
                    {{ providerLabel(feeds[kind]!.provider) }}
                </dd>
                <dt class="col-sm-4">Data freshness</dt>
                <dd class="col-sm-8">{{ feeds[kind]!.fresh ? 'Fresh' : 'Unavailable or stale' }}</dd>
                <dt class="col-sm-4">Last successful update</dt>
                <dd class="col-sm-8">{{ date(feeds[kind]!.fetched_at) }}</dd>
                <dt class="col-sm-4">Manual refresh</dt>
                <dd class="col-sm-8">
                    {{
                        feeds[kind]!.manual_refresh?.allowed
                            ? 'Available'
                            : stateText(feeds[kind]!.manual_refresh?.reason || 'unavailable')
                    }}
                    <span v-if="feeds[kind]!.manual_refresh?.retry_after_seconds">
                        · Retry in {{ feeds[kind]!.manual_refresh!.retry_after_seconds }} seconds</span
                    >
                </dd>
                <dt class="col-sm-4">Next refresh</dt>
                <dd class="col-sm-8">{{ date(feeds[kind]!.next_refresh) }}</dd>
                <dt class="col-sm-4">Source age / transport age</dt>
                <dd class="col-sm-8">
                    {{ feeds[kind]!.source_age_seconds ?? 'Not supplied' }} /
                    {{ feeds[kind]!.transport_age_seconds }} seconds
                </dd>
                <dt class="col-sm-4">Rate limit remaining</dt>
                <dd class="col-sm-8">
                    {{ feeds[kind]!.rate_remaining < 0 ? 'Not supplied' : feeds[kind]!.rate_remaining }}
                </dd>
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
            <p v-if="feeds[kind]!.attribution">{{ feeds[kind]!.attribution }}</p>
            <p v-if="kind === 'forecast'">
                Today: {{ dailyEnergy(0, false) }} Wh · Remaining: {{ dailyEnergy(0, true) }} Wh · Tomorrow:
                {{ dailyEnergy(1, false) }} Wh
            </p>
            <p v-if="kind === 'tariff'">
                Today's covered average: {{ dailyPrice(0) }} · Tomorrow's covered average: {{ dailyPrice(1) }}
            </p>
            <p v-if="feeds[kind]!.last_error" class="alert alert-warning" role="status">
                Last update error: {{ stateText(feeds[kind]!.last_error!) }}. Previous data is retained; current
                coverage and age determine planning usability.
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
                        <tr v-for="interval in preview(kind)" :key="interval.start">
                            <td>{{ date(interval.start) }} / {{ date(interval.end) }}</td>
                            <td>{{ value(kind, interval) }}</td>
                            <td v-if="kind === 'tariff'">
                                {{ price(interval.import_price_micros_per_kwh, feeds[kind]!.currency) }}
                            </td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <button
                v-if="feeds[kind]!.intervals.length > 8"
                type="button"
                class="btn btn-outline-secondary btn-sm mb-3"
                @click="expanded[kind] = !expanded[kind]"
            >
                {{ expanded[kind] ? 'Compact preview' : 'Show all future intervals' }}
            </button>
            <div v-if="drafts[kind]">
                <h3 class="h5">Provider settings</h3>
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
                    <label :for="`${kind}-source`" class="form-label">Source</label>
                    <select
                        :id="`${kind}-source`"
                        v-model="drafts[kind]!.provider"
                        class="form-select mb-3"
                        @change="selectProvider(kind)"
                    >
                        <option :value="kind === 'tariff' ? 'energy_charts' : 'forecast_solar'">
                            {{ kind === 'tariff' ? 'Energy-Charts · Germany / Luxembourg (DE-LU)' : 'Forecast.Solar' }}
                        </option>
                        <option value="normalized_push">Normalized JSON push</option>
                        <option value="normalized_http">Local normalized HTTP</option>
                    </select>
                    <template v-if="drafts[kind]!.provider === 'forecast_solar'">
                        <p>
                            Up to six independent planes. Coordinates are required. Azimuth: south 0°, east −90°, west
                            +90°. Public forecast: today and tomorrow, hourly. Native refreshes are at least two hours
                            apart.
                        </p>
                        <div class="table-responsive">
                            <table class="table table-sm">
                                <caption>
                                    PV planes · coordinates in decimal degrees, peak power in kWp
                                </caption>
                                <thead>
                                    <tr>
                                        <th>On</th>
                                        <th>Name</th>
                                        <th>Latitude</th>
                                        <th>Longitude</th>
                                        <th>Tilt °</th>
                                        <th>Azimuth °</th>
                                        <th>kWp</th>
                                        <th>Status</th>
                                        <th>Delete</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    <tr v-for="(plane, index) in drafts[kind]!.planes" :key="index">
                                        <td>
                                            <input
                                                v-model="plane.enabled"
                                                type="checkbox"
                                                :aria-label="`Enable plane ${index + 1}`"
                                            />
                                        </td>
                                        <td>
                                            <input
                                                v-model="plane.name"
                                                class="form-control"
                                                maxlength="32"
                                                :aria-label="`Plane ${index + 1} name`"
                                                required
                                            />
                                        </td>
                                        <td>
                                            <input
                                                :value="plane.latitude_e6 == null ? '' : plane.latitude_e6 / 1e6"
                                                type="number"
                                                class="form-control"
                                                min="-90"
                                                max="90"
                                                step="0.000001"
                                                :aria-label="`Plane ${index + 1} latitude`"
                                                required
                                                @input="plane.latitude_e6 = coordinate($event)"
                                            />
                                        </td>
                                        <td>
                                            <input
                                                :value="plane.longitude_e6 == null ? '' : plane.longitude_e6 / 1e6"
                                                type="number"
                                                class="form-control"
                                                min="-180"
                                                max="180"
                                                step="0.000001"
                                                :aria-label="`Plane ${index + 1} longitude`"
                                                required
                                                @input="plane.longitude_e6 = coordinate($event)"
                                            />
                                        </td>
                                        <td>
                                            <input
                                                v-model.number="plane.tilt"
                                                type="number"
                                                class="form-control"
                                                min="0"
                                                max="90"
                                                step="1"
                                                :aria-label="`Plane ${index + 1} tilt`"
                                                required
                                            />
                                        </td>
                                        <td>
                                            <input
                                                v-model.number="plane.azimuth"
                                                type="number"
                                                class="form-control"
                                                min="-180"
                                                max="180"
                                                step="1"
                                                :aria-label="`Plane ${index + 1} azimuth`"
                                                required
                                            />
                                        </td>
                                        <td>
                                            <input
                                                :value="plane.peak_w / 1000 || ''"
                                                type="number"
                                                class="form-control"
                                                min="0.001"
                                                max="1000"
                                                step="0.001"
                                                :aria-label="`Plane ${index + 1} kWp`"
                                                required
                                                @input="
                                                    plane.peak_w = Math.round(
                                                        Number(($event.target as HTMLInputElement).value) * 1000
                                                    )
                                                "
                                            />
                                        </td>
                                        <td>
                                            {{
                                                feeds[kind]!.plane_status?.[index]?.last_error ||
                                                (feeds[kind]!.plane_status?.[index]?.fetched_at ? 'Fetched' : 'Waiting')
                                            }}
                                        </td>
                                        <td>
                                            <button
                                                type="button"
                                                class="btn btn-outline-danger btn-sm"
                                                :aria-label="`Delete plane ${index + 1}`"
                                                @click="drafts[kind]!.planes.splice(index, 1)"
                                            >
                                                Delete
                                            </button>
                                        </td>
                                    </tr>
                                </tbody>
                            </table>
                        </div>
                        <button
                            type="button"
                            class="btn btn-secondary mb-3"
                            :disabled="drafts[kind]!.planes.length >= 6"
                            @click="addPlane()"
                        >
                            Add PV plane
                        </button>
                    </template>
                    <template v-if="kind === 'tariff'">
                        <div class="form-check mb-2">
                            <input
                                id="tariff-pricing-enabled"
                                v-model="drafts[kind]!.pricing.enabled"
                                class="form-check-input"
                                type="checkbox"
                            /><label for="tariff-pricing-enabled" class="form-check-label"
                                >Use my explicit variable tariff components</label
                            >
                        </div>
                        <p>
                            Effective import = (source price + grid fees/taxes + supplier markup) × (1 + VAT). Enter net
                            variable additions. The monthly fee is reported separately.
                        </p>
                        <label for="tariff-vat" class="form-label">VAT %</label
                        ><input
                            id="tariff-vat"
                            :value="toDisplay(drafts[kind]!.pricing.vat_basis_points, 100)"
                            type="number"
                            class="form-control mb-2"
                            min="0"
                            max="100"
                            step="0.01"
                            required
                            @input="
                                drafts[kind]!.pricing.vat_basis_points = toCanonical(
                                    ($event.target as HTMLInputElement).value,
                                    100
                                )
                            "
                        />
                        <div v-for="field in priceFields" :key="field.key">
                            <label :for="`tariff-${field.key}`" class="form-label"
                                >{{ field.label }} ({{ field.unit }})</label
                            ><input
                                :id="`tariff-${field.key}`"
                                :value="toDisplay(drafts[kind]!.pricing[field.key], field.scale)"
                                type="number"
                                class="form-control mb-2"
                                :min="field.min"
                                :max="field.max"
                                :step="field.step"
                                required
                                @input="
                                    drafts[kind]!.pricing[field.key] = toCanonical(
                                        ($event.target as HTMLInputElement).value,
                                        field.scale
                                    )
                                "
                            />
                        </div>
                    </template>
                    <label v-if="drafts[kind]!.provider === 'normalized_http'" :for="`${kind}-url`" class="form-label"
                        >Local bridge URL (HTTP), or leave empty for JSON uploads</label
                    >
                    <input
                        v-if="drafts[kind]!.provider === 'normalized_http'"
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
                        :disabled="busy[kind] || !feeds[kind]!.enabled || feeds[kind]!.provider === 'normalized_push'"
                        @click="refresh(kind)"
                    >
                        Refresh now
                    </button>
                </form>
                <details v-if="drafts[kind]!.provider === 'normalized_push'">
                    <summary>Advanced normalized JSON ingestion</summary>
                    <label :for="`${kind}-upload`" class="form-label mt-3"
                        >Upload normalized JSON (maximum 256 intervals / 40 kB)</label
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
            </div>
        </template>
    </CardElement>
</template>
<script setup lang="ts">
import { getCurrentInstance, onMounted, onUnmounted, reactive } from 'vue';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { priceFields, toDisplay, toCanonical } from '@/utils/tariffUnits';
type Kind = 'tariff' | 'forecast';
type Plane = {
    enabled: boolean;
    name: string;
    latitude_e6: number | null;
    longitude_e6: number | null;
    tilt: number;
    azimuth: number;
    peak_w: number;
};
type Pricing = {
    enabled: boolean;
    vat_basis_points: number;
    grid_micros_per_kwh: number;
    markup_micros_per_kwh: number;
    monthly_micros: number;
};
type Settings = {
    provider: string;
    planes: Plane[];
    pricing: Pricing;
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
    manual_refresh?: { allowed: boolean; reason: string | null; retry_after_seconds: number };
    revision: number;
    config: Settings;
    next_refresh: number;
    source_age_seconds: number | null;
    transport_age_seconds: number;
    rate_remaining: number;
    attribution?: string;
    plane_status?: { fetched_at: number; last_error: string | null }[];
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
function selectProvider(kind: Kind) {
    const draft = drafts[kind];
    if (!draft) return;
    draft.url = '';
    if (draft.provider === 'forecast_solar') {
        draft.refresh_seconds = 7200;
        draft.max_age_seconds = 10800;
    }
    if (draft.provider === 'energy_charts') draft.refresh_seconds = 3600;
}
function providerLabel(source: string) {
    return (
        (
            {
                energy_charts: 'Energy-Charts · DE-LU',
                forecast_solar: 'Forecast.Solar',
                normalized_http: 'Local HTTP',
                normalized_push: 'Normalized JSON push',
            } as Record<string, string>
        )[source] || source
    );
}
function coordinate(event: Event) {
    const value = (event.target as HTMLInputElement).value;
    return value === '' ? null : Math.round(Number(value) * 1e6);
}
function addPlane() {
    drafts.forecast?.planes.push({
        enabled: true,
        name: '',
        latitude_e6: null,
        longitude_e6: null,
        tilt: 0,
        azimuth: 0,
        peak_w: 0,
    });
}
function dayBounds(day: number) {
    const d = new Date();
    d.setHours(0, 0, 0, 0);
    d.setDate(d.getDate() + day);
    const start = d.getTime() / 1000;
    d.setDate(d.getDate() + 1);
    return [start, d.getTime() / 1000] as const;
}
function dailyEnergy(day: number, remaining: boolean) {
    const [start, end] = dayBounds(day);
    const from = remaining ? Math.max(start, Date.now() / 1000) : start;
    const intervals = feeds.forecast?.intervals || [];
    let cursor = from,
        ws = 0;
    for (const i of intervals) {
        const a = Math.max(from, i.start),
            b = Math.min(end, i.end);
        if (b <= a) continue;
        if (a > cursor) return 'Unknown (gap)';
        ws += (i.power_w || 0) * (b - a);
        cursor = b;
    }
    return cursor < end ? 'Unknown (partial horizon)' : (ws / 3600).toFixed(0);
}
function dailyPrice(day: number) {
    const [start, end] = dayBounds(day);
    let cost = 0,
        seconds = 0;
    for (const i of feeds.tariff?.intervals || []) {
        const duration = Math.max(0, Math.min(end, i.end) - Math.max(start, i.start));
        cost += (i.price_micros_per_kwh || 0) * duration;
        seconds += duration;
    }
    return seconds ? price(Math.round(cost / seconds), feeds.tariff?.currency) : 'Not published';
}
const domains: Kind[] = ['tariff', 'forecast'];
const expanded = reactive({ tariff: false, forecast: false });
function preview(kind: Kind) {
    return (feeds[kind]?.intervals || []).filter((i) => i.end > Date.now() / 1000).slice(0, expanded[kind] ? 256 : 8);
}
const feeds = reactive<Partial<Record<Kind, Feed>>>({});
const drafts = reactive<Partial<Record<Kind, Settings>>>({});
const errors = reactive({ tariff: '', forecast: '' });
const notices = reactive({ tariff: '', forecast: '' });
const busy = reactive({ tariff: false, forecast: false });
const settingFields = [
    { key: 'refresh_seconds', label: 'Refresh interval (native minimum applies)', min: 60, max: 86400 },
    {
        key: 'max_age_seconds',
        label: 'Maximum PV source age (tariff validity uses interval coverage)',
        min: 60,
        max: 604800,
    },
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
        busy: 'A provider request is queued or running',
        local_retry: 'Waiting for a short local retry',
        clock_invalid: 'Waiting for clock synchronization',
        transport_backoff: 'Waiting for request spacing or transport retry',
        provider_backoff: 'Provider retry delay is active',
        rate_limited: 'Provider quota or rate limit is active',
        not_enabled: 'Provider is disabled',
        invalid_config: 'Provider configuration does not permit polling',
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
    }).then(async (response) => {
        if (suffix === '/refresh' && response.status === 409) {
            const result = await response.json();
            throw new Error(
                `${stateText(result.reason || result.message || 'unavailable')}${result.retry_after_seconds ? `; retry in ${result.retry_after_seconds} seconds` : ''}.`
            );
        }
        return handleResponse(response, emitter, router, true);
    });
}
async function load(kind: Kind, reset = false) {
    try {
        let feed = (await api(kind)) as Feed;
        const previous = feeds[kind];
        if (!previous || reset || previous.revision !== feed.revision) feed = (await api(kind, '?all=1')) as Feed;
        else feed.intervals = previous.intervals;
        if (disposed) return;
        feeds[kind] = feed;
        if (!drafts[kind] || reset) drafts[kind] = structuredClone(feed.config);
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
        const result = await api(kind, suffix, data);
        if (result?.ok === false) throw new Error(result.message || result.reason || 'Request failed');
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
        if (file.size > 40000) throw new Error('JSON file exceeds 40 kB.');
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
