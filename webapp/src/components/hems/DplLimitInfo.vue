<template>
    <span class="d-block mb-2">
        {{ $t('redesign.dplTarget') }}:
        <strong v-if="!unavailable && data?.dpl && data.dpl.PLSTATE !== -1"
            >{{ $n(data.dpl.PLLIMIT, 'decimalNoDigits') }} W</strong
        >
        <span v-else>{{
            $t(!unavailable && data?.dpl?.PLSTATE === -1 ? 'hemsUi.disabled' : 'hemsUi.unavailable')
        }}</span>
        <span class="d-block small text-body-secondary">{{ $t('redesign.dplTargetHelp') }}</span>
    </span>
</template>
<script setup lang="ts">
import type { DynamicPowerLimiter } from '@/types/SolarChargerLiveDataStatus';
import { usePolledResource } from '@/utils/usePolledResource';
// The existing solar live API exposes DPL's last computed aggregate output target.
const { data, unavailable } = usePolledResource<{ dpl: DynamicPowerLimiter }>('/api/solarchargerlivedata/status');
</script>
