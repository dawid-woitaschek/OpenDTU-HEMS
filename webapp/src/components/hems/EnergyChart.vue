<template>
    <figure class="energy-chart mb-0" ref="container">
        <div class="d-flex flex-wrap justify-content-between gap-2 small mb-2">
            <span class="fw-semibold"
                >{{ label }} <span class="text-body-secondary">({{ unit }})</span></span
            >
            <label v-if="intervals.some((i) => finite(i.secondary))" class="form-check-label">
                <input v-model="showSource" type="checkbox" class="form-check-input me-1" />{{
                    t('hemsUi.sourcePrice')
                }}
            </label>
        </div>
        <svg
            :viewBox="`0 0 ${width} ${height}`"
            class="w-100 chart-surface"
            tabindex="0"
            role="img"
            :aria-label="`${label}. ${t('hemsUi.chartHelp')}`"
            @pointerdown="inspect($event, true)"
            @pointermove="inspect($event)"
            @keydown.left.prevent="move(-1)"
            @keydown.right.prevent="move(1)"
            @focus="focusCurrent"
        >
            <title>{{ label }} · {{ zone }}</title>
            <g v-for="tick in yTicks" :key="tick">
                <line
                    :x1="left"
                    :x2="width - right"
                    :y1="y(tick)"
                    :y2="y(tick)"
                    class="grid-line"
                    :class="{ 'zero-line': tick === 0 }"
                />
                <text :x="left - 6" :y="y(tick) + 4" text-anchor="end">
                    {{ tick.toLocaleString(locale, { maximumFractionDigits: 1 }) }}
                </text>
            </g>
            <g v-for="(day, index) in days" :key="day">
                <line :x1="x(day)" :x2="x(day)" :y1="top" :y2="bottom" class="day-line" />
                <text :x="x(day) + 5" y="16">
                    {{ t(index === 0 ? 'hemsUi.today' : 'hemsUi.tomorrow') }} ·
                    {{ new Date(day * 1000).toLocaleDateString(locale, { day: 'numeric', month: 'short' }) }}
                </text>
            </g>
            <g v-for="tick in xTicks" :key="tick">
                <text :x="x(tick)" :y="height - 8" text-anchor="middle">{{ time(tick) }}</text>
            </g>
            <rect
                v-for="i in visible.filter((i) => i.category && finite(i.value))"
                :key="`band-${i.start}`"
                :x="x(i.start)"
                :width="Math.max(0, x(i.end) - x(i.start))"
                :y="bottom + 4"
                height="5"
                :class="`period-${i.category}`"
            />
            <path v-if="showSource" :d="secondaryPath" class="source-line" />
            <path :d="primaryPath" class="primary-line" />
            <g v-if="now >= start && now < end">
                <line :x1="x(now)" :x2="x(now)" :y1="top" :y2="bottom" class="now-line" />
                <text :x="Math.min(width - right - 30, x(now) + 4)" :y="top + 12" class="now-label">
                    {{ t('hemsUi.now') }}
                </text>
            </g>
            <g v-if="selected">
                <rect
                    :x="x(selected.start)"
                    :width="Math.max(1, x(selected.end) - x(selected.start))"
                    :y="top"
                    :height="bottom - top"
                    class="inspection-band"
                />
                <circle
                    v-if="finite(selected.value)"
                    :cx="x((selected.start + selected.end) / 2)"
                    :cy="y(selected.value)"
                    r="4"
                    class="inspection-dot"
                />
            </g>
            <rect :x="left" :y="top" :width="width - left - right" :height="bottom - top + 10" fill="transparent" />
        </svg>
        <figcaption class="inspection border rounded p-2 mt-1" aria-live="polite">
            <template v-if="selected">
                <div class="small text-body-secondary">{{ date(selected.start) }} – {{ date(selected.end) }}</div>
                <strong class="fs-5">{{ display(selected.value) }}</strong>
                <span v-if="selected.category" class="ms-2 small">{{ t('hemsUi.' + selected.category) }}</span>
                <span v-if="showSource && finite(selected.secondary)" class="d-block small text-body-secondary"
                    >{{ t('hemsUi.sourcePrice') }}: {{ display(selected.secondary) }}</span
                >
            </template>
            <span v-else class="text-body-secondary">{{
                inspected ? t('hemsUi.noInterval') : t('hemsUi.chartHelp')
            }}</span>
        </figcaption>
        <div class="d-flex flex-wrap gap-3 small mt-2" v-if="intervals.some((i) => i.category)">
            <span v-for="category in ['cheap', 'normal', 'expensive'] as const" :key="category"
                ><i class="legend-swatch" :class="`period-${category}`"></i>{{ t('hemsUi.' + category) }}</span
            >
        </div>
        <p class="small text-body-secondary mt-2 mb-0">{{ zone }} · {{ t('hemsUi.chartHelp') }}</p>
    </figure>
</template>
<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { dayBounds, finite, stepPath, type ChartInterval } from '@/utils/hemsPresentation';
const props = defineProps<{
    intervals: ChartInterval[];
    now: number;
    label: string;
    unit: string;
    display: (value: number | null | undefined) => string;
}>();
const { t, locale } = useI18n();
const container = ref<HTMLElement>();
const width = ref(600),
    height = 250,
    left = 48,
    right = 14,
    top = 30,
    bottom = 214;
const showSource = ref(false),
    inspected = ref<number | null>(null);
const zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
const start = computed(() => dayBounds(props.now)[0]);
const end = computed(() => dayBounds(props.now, 1)[1]);
const days = computed(() => [start.value, dayBounds(props.now, 1)[0]]);
const visible = computed(() =>
    props.intervals
        .filter((i) => i.end > start.value && i.start < end.value)
        .map((i) => ({ ...i, start: Math.max(start.value, i.start), end: Math.min(end.value, i.end) }))
);
const values = computed(() =>
    visible.value.flatMap((i) => [i.value, ...(showSource.value ? [i.secondary] : [])]).filter(finite)
);
const range = computed(() => {
    const low = Math.min(0, ...values.value),
        high = Math.max(0, ...values.value);
    const span = high - low || 1;
    return [low < 0 ? low - span * 0.08 : 0, high + span * 0.12] as const;
});
const x = (utc: number) => left + ((utc - start.value) / (end.value - start.value)) * (width.value - left - right);
const y = (value: number) => bottom - ((value - range.value[0]) / (range.value[1] - range.value[0])) * (bottom - top);
const yTicks = computed(() => [...new Set([range.value[0], 0, (range.value[0] + range.value[1]) / 2, range.value[1]])]);
const xTicks = computed(() => {
    const result: number[] = [];
    const step = width.value < 480 ? 12 : width.value < 800 ? 6 : 3;
    for (const day of days.value) {
        // Build wall-clock ticks per day; epoch positions remain DST-safe.
        for (let hour = step; hour < 24; hour += step) {
            const d = new Date(day * 1000);
            d.setHours(hour);
            result.push(d.getTime() / 1000);
        }
    }
    return [...new Set(result)];
});
const primaryPath = computed(() => stepPath(visible.value, x, y));
const secondaryPath = computed(() => stepPath(visible.value, x, y, true));
const selected = computed(() => {
    const utc = inspected.value ?? props.now;
    return visible.value.find((i) => i.start <= utc && utc < i.end);
});
const time = (utc: number) =>
    new Date(utc * 1000).toLocaleTimeString(locale.value, { hour: '2-digit', minute: '2-digit' });
const date = (utc: number) =>
    new Date(utc * 1000).toLocaleString(locale.value, {
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        timeZoneName: 'short',
    });
function inspect(event: PointerEvent, down = false) {
    if (event.pointerType !== 'mouse' && !down && !event.buttons) return;
    const svg = event.currentTarget as SVGSVGElement;
    if (down) svg.setPointerCapture(event.pointerId);
    const bounds = svg.getBoundingClientRect();
    const position = ((event.clientX - bounds.left) / bounds.width) * width.value;
    if (position < left || position > width.value - right) return;
    inspected.value = start.value + ((position - left) / (width.value - left - right)) * (end.value - start.value);
}
function focusCurrent() {
    if (!selected.value) inspected.value = visible.value[0]?.start ?? null;
}
function move(direction: number) {
    const index = selected.value ? visible.value.indexOf(selected.value) : -1;
    const next = Math.max(0, Math.min(visible.value.length - 1, index + direction));
    inspected.value = visible.value[next]?.start ?? null;
}
watch(start, () => {
    inspected.value = null;
});
let observer: ResizeObserver | undefined;
let resizeFrame = 0;
onMounted(() => {
    observer = new ResizeObserver((entries) => {
        const next = Math.max(240, Math.round(entries[0]?.contentRect.width || 600));
        if (next !== width.value) {
            cancelAnimationFrame(resizeFrame);
            resizeFrame = requestAnimationFrame(() => {
                width.value = next;
            });
        }
    });
    if (container.value) observer.observe(container.value);
});
onUnmounted(() => {
    observer?.disconnect();
    cancelAnimationFrame(resizeFrame);
});
</script>
<style scoped>
.energy-chart {
    min-width: 0;
}
.chart-surface {
    display: block;
    touch-action: pan-y;
    color: var(--bs-body-color);
}
.chart-surface:focus-visible {
    outline: 2px solid var(--bs-primary);
    outline-offset: 2px;
}
svg text {
    fill: var(--bs-secondary-color);
    font-size: 11px;
}
.grid-line {
    stroke: var(--bs-border-color);
    stroke-width: 0.5;
}
.zero-line {
    stroke: var(--bs-secondary-color);
    stroke-width: 1;
}
.day-line {
    stroke: var(--bs-border-color);
    stroke-dasharray: 3 3;
}
.primary-line {
    fill: none;
    stroke: var(--bs-primary);
    stroke-width: 2;
    stroke-linejoin: round;
}
.source-line {
    fill: none;
    stroke: var(--bs-secondary-color);
    stroke-width: 1.2;
    stroke-dasharray: 4 3;
}
.now-line {
    stroke: var(--bs-body-color);
    stroke-dasharray: 4 3;
}
.now-label {
    fill: var(--bs-body-color);
}
.inspection-band {
    fill: var(--bs-primary);
    opacity: 0.08;
}
.inspection-dot {
    fill: var(--bs-primary);
}
.inspection {
    min-height: 4.5rem;
    overflow-wrap: anywhere;
}
.period-cheap {
    fill: #538b6d;
    background: #538b6d;
}
.period-normal {
    fill: #a09b82;
    background: #a09b82;
}
.period-expensive {
    fill: #b56d6d;
    background: #b56d6d;
}
.legend-swatch {
    display: inline-block;
    width: 1.2rem;
    height: 0.3rem;
    margin-right: 0.35rem;
    vertical-align: middle;
}
</style>
