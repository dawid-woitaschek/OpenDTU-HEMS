<template>
    <details class="border rounded p-3 mb-3">
        <summary>{{ $t('redesign.powerLimit') }} <HelpHint :text="$t('redesign.limitHelp')" /></summary>
        <DplLimitInfo class="mt-3" />
        <ul class="mt-3 mb-0">
            <li v-for="inverter in inverters" :key="inverter.serial">
                {{ inverter.name }}:
                {{ inverter.limit_absolute >= 0 ? $n(inverter.limit_absolute, 'decimalNoDigits') + ' W · ' : ''
                }}{{ inverter.limit_relative >= 0 ? $n(inverter.limit_relative, 'decimalOneDigit') + ' %' : '—' }}
                <span v-if="!inverter.reachable" class="text-warning-emphasis"> · {{ $t('hemsUi.unreachable') }}</span>
            </li>
        </ul>
    </details>
</template>
<script setup lang="ts">
import type { Inverter } from '@/types/LiveDataStatus';
import DplLimitInfo from './DplLimitInfo.vue';
import HelpHint from '@/components/HelpHint.vue';
defineProps<{ inverters: Inverter[] }>();
</script>
