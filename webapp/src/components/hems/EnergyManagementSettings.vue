<template>
    <fieldset class="border rounded p-3 mb-3">
        <legend class="float-none w-auto fs-6 px-1 label-with-help">
            {{ t('hemsUi.energyManagement') }} <HelpHint :text="t('hemsUi.managementHint')" />
        </legend>
        <button type="button" class="btn btn-outline-secondary mb-3" @click="applyReference">
            {{ t('hemsUi.referencePreset') }}
        </button>
        <label class="form-check-label d-block mb-3"
            ><input
                id="energy-management-enabled"
                v-model="model.enabled"
                type="checkbox"
                class="form-check-input me-2"
            />{{ t('hemsUi.coordinateOptIn') }}</label
        >
        <FieldLabel
            for-id="energy-preference"
            :text="t('hemsUi.operatingPreference')"
            :help="t('hemsUi.preferenceHint')"
            label-class="form-label"
        />
        <select id="energy-preference" v-model.number="model.preference" class="form-select mb-3">
            <option :value="0">{{ t('hemsUi.economicPreference') }}</option>
            <option :value="1">{{ t('hemsUi.localFirst') }}</option>
            <option :value="2">{{ t('hemsUi.exportFirst') }}</option>
        </select>
        <div v-for="field in permissions" :key="field" class="form-check mb-2">
            <input :id="`energy-${field}`" v-model="model[field]" type="checkbox" class="form-check-input" />
            <label :for="`energy-${field}`" class="form-check-label">{{ t('hemsUi.' + field) }}</label>
        </div>
        <label for="energy-net-export" class="form-label mt-2">{{ t('hemsUi.netExportLimit') }} (W)</label>
        <input
            id="energy-net-export"
            v-model.number="model.max_export_w"
            type="number"
            min="0"
            max="100000"
            step="1"
            class="form-control mb-3"
            required
        />
        <FieldLabel
            for-id="energy-load-source"
            :text="t('hemsUi.householdLoad')"
            :help="t('hemsUi.learningHint')"
            label-class="form-label"
        />
        <select id="energy-load-source" v-model="model.automatic_load" class="form-select">
            <option :value="false">{{ t('hemsUi.fixedLoad') }}</option>
            <option :value="true">{{ t('hemsUi.automaticLoad') }}</option>
        </select>
        <p v-if="model.enabled && model.wear_source === 0" class="alert alert-warning">
            {{ t('hemsUi.wearAcknowledgementHint') }}
        </p>
        <details class="mt-3">
            <summary>
                <span class="label-with-help"
                    >{{ t('hemsUi.advancedSettings') }} <HelpHint :text="t('hemsUi.economicsHint')"
                /></span>
            </summary>
            <label for="energy-wear-source" class="form-label">{{ t('hemsUi.wearSource') }}</label>
            <select
                id="energy-wear-source"
                v-model.number="model.wear_source"
                class="form-select mb-3"
                @change="model.wear_source === 0 && (model.degradation_micros = 0)"
            >
                <option :value="0">{{ t('hemsUi.wearUnknown') }}</option>
                <option :value="1">{{ t('hemsUi.wearEstimated') }}</option>
                <option :value="2">{{ t('hemsUi.wearConfigured') }}</option>
            </select>
            <div class="row g-3">
                <div v-for="field in advanced" :key="field.key" class="col-sm-6">
                    <label :for="`energy-${field.key}`" class="form-label"
                        >{{ t('hemsUi.' + field.key) }} ({{ field.unit }})</label
                    >
                    <input
                        :id="`energy-${field.key}`"
                        :value="model[field.key] / field.scale"
                        :disabled="
                            (field.key === 'degradation_micros' && model.wear_source === 0) ||
                            (field.key === 'terminal_price_micros' && !model.terminal_price_configured)
                        "
                        type="number"
                        :min="field.min"
                        :max="field.max"
                        :step="field.step"
                        class="form-control"
                        required
                        @input="
                            model[field.key] = Math.round(
                                Number(($event.target as HTMLInputElement).value) * field.scale
                            )
                        "
                    />
                </div>
            </div>
            <div class="form-check mt-3">
                <input
                    id="energy-terminal-price"
                    v-model="model.terminal_price_configured"
                    type="checkbox"
                    class="form-check-input"
                /><FieldLabel
                    for-id="energy-terminal-price"
                    :text="t('hemsUi.explicitTerminalPrice')"
                    :help="t('hemsUi.terminalHint')"
                    label-class="form-check-label"
                />
            </div>
        </details>
    </fieldset>
</template>
<script setup lang="ts">
import FieldLabel from '@/components/FieldLabel.vue';
import HelpHint from '@/components/HelpHint.vue';
import { useI18n } from 'vue-i18n';
import { referencePreset, type EnergyManagement } from '@/utils/hemsEnergyManagement';
const model = defineModel<EnergyManagement>({ required: true });
const { t } = useI18n();
const emit = defineEmits<{ reference: [] }>();
function applyReference() {
    model.value = referencePreset(model.value);
    emit('reference');
}
const permissions = ['pv_export', 'battery_export', 'ev_protection'] as const;
const advanced = [
    { key: 'pv_ac_limit_w', unit: 'W', scale: 1, min: 0, max: 100000, step: 1 },
    { key: 'degradation_micros', unit: 'ct/kWh EFC', scale: 10000, min: 0, max: 1000, step: 0.0001 },
    { key: 'buffer_micros', unit: 'ct/kWh EFC', scale: 10000, min: 0, max: 1000, step: 0.0001 },
    { key: 'efc_capacity_wh', unit: 'Wh', scale: 1, min: 0, max: 1000000, step: 1 },
    { key: 'terminal_hours', unit: 'h', scale: 1, min: 1, max: 48, step: 1 },
    { key: 'terminal_price_micros', unit: 'ct/kWh', scale: 10000, min: 0, max: 1000, step: 0.0001 },
    { key: 'hysteresis_micros', unit: 'ct', scale: 10000, min: 0, max: 1000, step: 0.0001 },
] as const;
</script>
