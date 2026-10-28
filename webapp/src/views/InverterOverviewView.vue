<template>
    <BasePage :title="$t('redesign.inverters')" :isLoading="loading && !data" show-reload @reload="reload">
        <div class="d-flex flex-wrap gap-2 mb-3">
            <RouterLink to="/live" class="btn btn-outline-primary btn-sm">{{ $t('redesign.detailedLive') }}</RouterLink>
            <RouterLink to="/plant/inverters/configuration" class="btn btn-outline-secondary btn-sm">{{
                $t('redesign.configuration')
            }}</RouterLink>
        </div>
        <div class="d-flex flex-wrap gap-3 align-items-center border rounded p-3 mb-3">
            <DplLimitInfo />
            <RouterLink to="/energy/power-control" class="small">{{ $t('redesign.powerControl') }}</RouterLink>
        </div>
        <p v-if="!inverters.length" class="text-body-secondary">
            {{ $t(unavailable ? 'hemsUi.unavailable' : 'redesign.noInverters') }}
        </p>
        <div class="row g-3">
            <div v-for="inv in inverters" :key="inv.serial" class="col-12 col-xl-6">
                <section class="card h-100 inverter-summary" :data-inverter="inv.serial">
                    <div class="card-header d-flex flex-wrap justify-content-between gap-2">
                        <h2 class="h6 mb-0">{{ inv.name }}</h2>
                        <span
                            class="badge"
                            :class="state(inv) === 'reachable' ? 'text-bg-success' : 'text-bg-secondary'"
                            >{{ $t('redesign.' + state(inv)) }}</span
                        >
                    </div>
                    <div class="card-body">
                        <div class="small text-body-secondary mb-2">{{ $t('hems.DataAge') }}: {{ age(inv) }}</div>
                        <p v-if="state(inv) !== 'reachable'" class="small text-warning-emphasis">
                            {{ $t('redesign.inverterMeasurementsUnavailable') }}
                        </p>
                        <dl class="row mb-2">
                            <dt class="col-7 fw-normal">{{ $t('redesign.acOutput') }}</dt>
                            <dd class="col-5 fw-semibold">{{ value(inv, inv.AC?.[0]?.Power?.v, 'W') }}</dd>
                            <dt class="col-7 fw-normal">{{ $t('redesign.confirmedLimit') }}</dt>
                            <dd class="col-5">
                                {{ value(inv, inv.limit_relative, '%')
                                }}<span v-if="state(inv) === 'reachable' && inv.limit_absolute >= 0">
                                    · {{ value(inv, inv.limit_absolute, 'W') }}</span
                                >
                            </dd>
                            <dt class="col-7 fw-normal">{{ $t('storage.Temperature') }}</dt>
                            <dd class="col-5">{{ value(inv, inverterTemperature(inv), '°C') }}</dd>
                            <dt class="col-7 fw-normal label-with-help">
                                {{ $t('redesign.recordedEvents') }} <HelpHint :text="$t('redesign.eventHistoryHelp')" />
                            </dt>
                            <dd class="col-5">{{ inv.events >= 0 ? inv.events : '—' }}</dd>
                        </dl>
                        <RouterLink
                            :to="{ path: '/live', query: { inverter: inv.serial } }"
                            class="btn btn-outline-primary btn-sm"
                            >{{ $t('redesign.detailedLive') }}</RouterLink
                        >
                    </div>
                </section>
            </div>
        </div>
    </BasePage>
</template>
<script setup lang="ts">
import { onMounted } from 'vue';
import { useI18n } from 'vue-i18n';
import BasePage from '@/components/BasePage.vue';
import HelpHint from '@/components/HelpHint.vue';
import DplLimitInfo from '@/components/hems/DplLimitInfo.vue';
import { useLiveData } from '@/utils/liveData';
import { inverterState, inverterTemperature } from '@/utils/inverterPresentation';
import type { Inverter } from '@/types/LiveDataStatus';
const { data, inverters, connection, loading, unavailable, reload } = useLiveData();
const { locale } = useI18n();
onMounted(reload);
const state = (inv: Inverter) => inverterState(inv, connection.value === 'connected' && !unavailable.value);
const value = (inv: Inverter, number: number | null | undefined, unit: string) =>
    state(inv) === 'reachable' && number != null && Number.isFinite(number) && (unit !== '%' || number >= 0)
        ? `${number.toLocaleString(locale.value, { maximumFractionDigits: 1 })} ${unit}`
        : '—';
const age = (inv: Inverter) =>
    Number.isFinite(inv.data_age_ms) ? `${Math.floor(inv.data_age_ms / 1000).toLocaleString(locale.value)} s` : '—';
</script>
