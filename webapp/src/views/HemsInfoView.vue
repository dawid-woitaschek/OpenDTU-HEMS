<template>
    <BasePage :title="$t('hemsUi.overviewTitle')" :isLoading="loading" show-reload @reload="load">
        <HemsNav />
        <div v-if="offline" class="alert alert-warning" role="alert">{{ $t('hems.StatusUnavailable') }}</div>
        <template v-if="status">
            <div class="d-flex flex-wrap align-items-center justify-content-between gap-3 mb-4">
                <div>
                    <span
                        class="badge fs-6 me-2"
                        :class="
                            offline
                                ? 'text-bg-secondary'
                                : status.state === 'Active'
                                  ? 'text-bg-primary'
                                  : 'text-bg-secondary'
                        "
                        >{{ offline ? $t('hems.Unknown') : $t('hems.' + status.state) }}</span
                    >
                    <span :class="issues ? 'text-warning-emphasis' : 'text-body-secondary'">{{
                        $t(
                            offline
                                ? 'hemsUi.connectionLost'
                                : issues
                                  ? 'hemsUi.needsAttention'
                                  : 'hemsUi.devicesHealthy'
                        )
                    }}</span>
                </div>
                <RouterLink to="/hems/settings" class="btn btn-outline-primary btn-sm">{{
                    $t('hemsUi.settings')
                }}</RouterLink>
            </div>
            <div class="row g-3">
                <div v-for="card in cards" :key="card.name" class="col-6 col-xl-3">
                    <section class="card h-100">
                        <div class="card-body">
                            <h2 class="h6 text-body-secondary d-flex align-items-center gap-2">
                                <component :is="card.icon" />{{ $t('hemsUi.' + card.name) }}
                            </h2>
                            <div class="fs-2 fw-semibold lh-sm my-2">{{ card.value }}</div>
                            <div class="fw-medium">{{ $t('hemsUi.' + card.state) }}</div>
                            <div v-if="card.detail" class="small text-body-secondary mt-1">{{ card.detail }}</div>
                            <div v-if="card.health" class="small text-warning-emphasis mt-2">
                                {{ $t('hemsUi.' + card.health) }}
                            </div>
                        </div>
                    </section>
                </div>
            </div>
            <CardElement :text="$t('hemsUi.automation')" textVariant="text-bg-primary" add-space>
                <div class="row g-3">
                    <div class="col-md-6">
                        <h2 class="h6">{{ $t('hemsUi.evProtection') }}</h2>
                        <div class="fw-semibold">{{ $t('hemsUi.' + protection) }}</div>
                        <p
                            v-if="!offline && status.shadow.ev_protection.state === 'waiting'"
                            class="small text-body-secondary mb-0"
                        >
                            {{ $t('hemsUi.protectionWaiting') }}
                        </p>
                        <p
                            v-else-if="!offline && !status.shadow.ev_protection.charging_known"
                            class="small text-body-secondary mb-0"
                        >
                            {{ $t('hemsUi.evStateUnknown') }}
                        </p>
                    </div>
                    <div class="col-md-6">
                        <dl class="row mb-0">
                            <dt class="col-6 fw-normal">{{ $t('hemsUi.storagePermission') }}</dt>
                            <dd class="col-6">{{ permission('storage') }}</dd>
                            <dt class="col-6 fw-normal">{{ $t('hemsUi.evsePermission') }}</dt>
                            <dd class="col-6">{{ permission('evse') }}</dd>
                        </dl>
                    </div>
                </div>
                <p class="small text-body-secondary mb-0 mt-3">{{ $t('hemsUi.modeSummary' + status.state) }}</p>
            </CardElement>
        </template>
        <HemsEnergyPanel view="overview" @health="providerIssue = $event" />
        <HemsStorageSchedulePanel />
        <HemsEvPlanPanel view="overview" />
    </BasePage>
</template>
<script setup lang="ts">
import { computed, getCurrentInstance, markRaw, onMounted, onUnmounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRouter } from 'vue-router';
import { BIconPlug, BIconSun, BIconBatteryHalf, BIconEvStation } from 'bootstrap-icons-vue';
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import HemsEvPlanPanel from '@/components/hems/HemsEvPlanPanel.vue';
import HemsNav from '@/components/hems/HemsNav.vue';
import HemsEnergyPanel from '@/components/HemsEnergyPanel.vue';
import HemsStorageSchedulePanel from '@/components/hems/HemsStorageSchedulePanel.vue';
import type { HemsStatus, HemsSignal } from '@/types/HemsStatus';
import { signalValue } from '@/utils/hemsPresentation';
import { authHeader, handleResponse } from '@/utils/authentication';
const { t, locale } = useI18n();
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
const status = ref<HemsStatus>(),
    loading = ref(true),
    offline = ref(false);
const icons = [BIconPlug, BIconSun, BIconBatteryHalf, BIconEvStation].map(markRaw);
const val = <T,>(signal?: HemsSignal<T>) => signalValue(signal, offline.value);
const number = (value: number | null, unit: string) =>
    value === null ? '—' : `${value.toLocaleString(locale.value, { maximumFractionDigits: 1 })} ${unit}`;
const power = (value: number | null) =>
    value === null
        ? '—'
        : number(
              Math.abs(value) >= 1000 ? Math.abs(value) / 1000 : Math.abs(value),
              Math.abs(value) >= 1000 ? 'kW' : 'W'
          );
function deviceHealth(name: 'storage' | 'warp') {
    const d = status.value?.[name];
    if (offline.value) return 'connectionLost';
    if (!d?.configured) return 'notConfigured';
    if (!d.enabled) return 'disabled';
    if (!d.reachable) return 'unreachable';
    if (!d.valid) return 'waitingData';
    if (d.stale) return 'outdated';
    if (name === 'storage' && val(status.value?.normalized.storage.fault_active)) return 'deviceFault';
    if (name === 'storage' && val(status.value?.normalized.storage.alarm_active)) return 'deviceAlarm';
    return '';
}
const cards = computed(() => {
    const n = status.value?.normalized;
    const grid = val(n?.grid.power_w),
        pv = val(n?.pv.power_w);
    const charging = val(n?.storage.charging),
        discharging = val(n?.storage.discharging);
    const ev = val(n?.evse.charging),
        connected = val(n?.evse.vehicle_connected);
    const signalHealth = (signal?: HemsSignal<unknown>) =>
        offline.value
            ? 'connectionLost'
            : signal?.quality === 'stale'
              ? 'outdated'
              : signal?.value == null || signal?.quality !== 'fresh'
                ? 'waitingData'
                : '';
    return [
        {
            name: 'grid',
            value: power(grid),
            state: grid === null ? 'unavailable' : grid > 0 ? 'importing' : grid < 0 ? 'exporting' : 'balanced',
            detail: '',
            health: signalHealth(n?.grid.power_w),
            icon: icons[0],
        },
        {
            name: 'pv',
            value: power(pv),
            state: pv === null ? 'unavailable' : pv > 0 ? 'producing' : 'idle',
            detail: '',
            health: signalHealth(n?.pv.power_w),
            icon: icons[1],
        },
        {
            name: 'battery',
            value: number(val(n?.storage.soc_pct), '%'),
            state:
                charging === true
                    ? 'charging'
                    : discharging === true
                      ? 'discharging'
                      : charging === false && discharging === false
                        ? 'idle'
                        : 'unavailable',
            detail: `${t('storage.AcPower')}: ${power(val(n?.storage.ac_power_w))}`,
            health: deviceHealth('storage') || signalHealth(n?.storage.soc_pct),
            icon: icons[2],
        },
        {
            name: 'evCharger',
            value: power(val(n?.evse.power_w)),
            state: ev === true ? 'charging' : ev === false ? 'notCharging' : 'unavailable',
            detail: `${t(connected === null ? 'hemsUi.vehicleUnknown' : connected ? 'hemsUi.vehicleConnected' : 'hemsUi.vehicleDisconnected')} · ${number(val(n?.evse.allowed_current_a), 'A')}`,
            health: deviceHealth('warp'),
            icon: icons[3],
        },
    ];
});
const providerIssue = ref(false);
const issues = computed(
    () => providerIssue.value || cards.value.some((c) => c.health && !['notConfigured', 'disabled'].includes(c.health))
);
const protection = computed(() => {
    const p = status.value?.shadow.ev_protection;
    if (offline.value || !p) return 'unavailable';
    if (p.blocking_storage_discharge) return 'protectionBlocking';
    if (p.would_block_storage_discharge) return 'protectionShadow';
    return p.charging_known ? 'protectionIdle' : 'unavailable';
});
function permission(name: 'storage' | 'evse') {
    return offline.value
        ? t('hemsUi.unavailable')
        : t(status.value?.actuation[name].control_mode === 'hems' ? 'hemsUi.hemsControl' : 'hemsUi.monitorOnly');
}
let timer: ReturnType<typeof setTimeout> | undefined,
    disposed = false,
    fetching = false;
async function load() {
    if (fetching) return;
    fetching = true;
    try {
        const response = await fetch('/api/hems/status', { headers: authHeader() });
        const data = await handleResponse(response, emitter, router);
        if (!disposed) {
            status.value = data;
            offline.value = false;
        }
    } catch {
        offline.value = true;
    } finally {
        fetching = false;
        loading.value = false;
    }
}
async function poll() {
    await load();
    if (!disposed) timer = setTimeout(poll, 3000);
}
onMounted(poll);
onUnmounted(() => {
    disposed = true;
    clearTimeout(timer);
});
</script>
