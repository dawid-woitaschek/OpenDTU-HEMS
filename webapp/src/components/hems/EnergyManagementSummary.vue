<template>
    <div class="small border rounded p-3 my-3">
        <p>
            {{ t('hemsUi.householdLoad') }}:
            {{ t(plan.load_forecast_source === 'local_profile' ? 'hemsUi.automaticLoad' : 'hemsUi.fixedLoad') }} ·
            {{ plan.load_learned_slots || 0 }}/{{ plan.load_total_slots || 0 }} {{ t('hemsUi.coveredSlots') }}
        </p>
        <p>{{ t('hemsUi.chargeRationale') }} {{ plan.charge_target_soc_pct?.toFixed(1) || '—' }}%</p>
        <template v-if="plan.dispatch">
            <p>
                {{ t('hemsUi.plannedCommanded') }}: {{ plan.dispatch.planned_battery_w || 0 }} W /
                {{
                    ((plan.dispatch.commanded_action || plan.dispatch.last_action) === 'set_discharge_power_w'
                        ? -1
                        : 1) * (plan.dispatch.commanded_power_w ?? plan.dispatch.last_power_w ?? 0)
                }}
                W
            </p>
            <p v-if="plan.config.energy_management?.enabled">
                {{
                    t(
                        plan.dispatch.export_enforcement_available
                            ? 'hemsUi.enforcementAvailable'
                            : 'hemsUi.enforcementUnavailable'
                    )
                }}
                · {{ reason(plan.dispatch.coordination_state || '') }}
            </p>
        </template>
        <template v-if="plan.cost_breakdown">
            <div>
                {{ t('hemsUi.importCost') }}: {{ money(plan.cost_breakdown.import_nano) }} ·
                {{ t('hemsUi.exportRevenue') }}: {{ money(plan.cost_breakdown.export_nano) }}
            </div>
            <div>
                {{ t('hemsUi.degradation_micros') }}:
                {{
                    plan.cost_breakdown.wear_source
                        ? money(plan.cost_breakdown.degradation_nano)
                        : t('hemsUi.wearUnknown')
                }}
                · {{ t('hemsUi.buffer_micros') }}: {{ money(plan.cost_breakdown.buffer_nano) }}
            </div>
            <div>
                {{ t('hemsUi.inventoryValue') }}: {{ money(plan.cost_breakdown.terminal_inventory_change_nano) }} ·
                {{ t('hemsUi.curtailment') }}: {{ plan.cost_breakdown.curtailed_wh.toFixed(0) }} Wh
            </div>
        </template>
    </div>
</template>
<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import type { EnergyManagement } from '@/utils/hemsEnergyManagement';
const { t, te, locale } = useI18n();
const props = defineProps<{
    plan: {
        config: { enabled: boolean; energy_management?: EnergyManagement };
        dispatch?: {
            planned_battery_w?: number;
            commanded_action?: string;
            commanded_power_w?: number;
            last_action?: string;
            last_power_w?: number;
            export_enforcement_available?: boolean;
            coordination_state?: string;
        };
        currency?: string;
        load_forecast_source?: string;
        load_learned_slots?: number;
        load_total_slots?: number;
        charge_target_soc_pct?: number;
        cost_breakdown?: {
            import_nano: number;
            export_nano: number;
            degradation_nano: number;
            buffer_nano: number;
            terminal_inventory_change_nano: number;
            curtailed_wh: number;
            wear_source: number;
        };
    };
}>();
const money = (value: number) =>
    `${(value / 1e9).toLocaleString(locale.value, { maximumFractionDigits: 4 })} ${props.plan.currency || ''}`;
const reason = (value: string) =>
    te('hemsUi.optimizerStates.' + value) ? t('hemsUi.optimizerStates.' + value) : t('hemsUi.optimizerUnavailable');
</script>
