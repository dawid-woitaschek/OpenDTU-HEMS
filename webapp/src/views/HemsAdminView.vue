<template>
    <BasePage :title="$t('hems.Settings')" :isLoading="loading">
        <p v-if="error" class="alert alert-danger" role="alert">{{ error }}</p>
        <p v-if="notice" class="alert alert-success" role="status">{{ notice }}</p>
        <p v-if="warning" class="alert alert-warning" role="status">{{ warning }}</p>
        <template v-if="config">
            <div class="row g-3">
                <div class="col-12 col-lg-4">
                    <CardElement class="h-100" :text="t('hemsUi.general')" textVariant="text-bg-primary">
                        <form @submit.prevent="saveMode">
                            <FieldLabel
                                for-id="hems-mode"
                                :text="t('hemsUi.hemsMode')"
                                :help="t('hemsUi.modeHint')"
                                label-class="form-label"
                            />
                            <select id="hems-mode" v-model="config.mode" class="form-select mb-2">
                                <option v-for="mode in ['Disabled', 'Monitor', 'Active']" :key="mode" :value="mode">
                                    {{ $t('hems.' + mode) }}
                                </option>
                            </select>

                            <button class="btn btn-primary" :disabled="busy">{{ t('hemsUi.save') }}</button>
                        </form>
                    </CardElement>
                </div>
                <div class="col-12 col-lg-8">
                    <CardElement
                        :text="t('hemsUi.deviceControl')"
                        :help="t('hemsUi.permissionHint')"
                        textVariant="text-bg-primary"
                        class="h-100"
                    >
                        <div class="row g-3 mb-3">
                            <form class="col-md-6" @submit.prevent="saveStorage">
                                <label for="storage-control" class="form-label">{{
                                    t('hemsUi.storagePermission')
                                }}</label
                                ><select
                                    id="storage-control"
                                    v-model="config.storage.control_mode"
                                    class="form-select mb-2"
                                >
                                    <option value="monitor">{{ t('hemsUi.monitorOnly') }}</option>
                                    <option value="hems">{{ t('hemsUi.hemsControl') }}</option></select
                                ><button class="btn btn-primary" :disabled="busy">{{ t('hemsUi.save') }}</button>
                            </form>
                            <form v-if="binding" class="col-md-6" @submit.prevent="saveEvse">
                                <label for="evse-control" class="form-label">{{ t('hemsUi.evsePermission') }}</label
                                ><select id="evse-control" v-model="binding.control_mode" class="form-select mb-2">
                                    <option value="monitor">{{ t('hemsUi.monitorOnly') }}</option>
                                    <option value="hems">{{ t('hemsUi.hemsControl') }}</option></select
                                ><button class="btn btn-primary" :disabled="busy">{{ t('hemsUi.save') }}</button>
                            </form>
                        </div>
                        <div class="d-flex gap-3">
                            <RouterLink to="/plant/battery">{{ t('redesign.battery') }}</RouterLink
                            ><RouterLink to="/plant/wallbox">{{ t('redesign.wallbox') }}</RouterLink>
                        </div>
                    </CardElement>
                </div>
            </div>
            <HemsEnergyPanel view="settings" />
            <HemsOptimizerPanel view="settings" />
        </template>
    </BasePage>
</template>
<script setup lang="ts">
import { getCurrentInstance, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import FieldLabel from '@/components/FieldLabel.vue';
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import { useI18n } from 'vue-i18n';
const { t } = useI18n();
import HemsEnergyPanel from '@/components/HemsEnergyPanel.vue';
import HemsOptimizerPanel from '@/components/HemsOptimizerPanel.vue';
import type { HemsConfig, HemsDeviceControlMode } from '@/types/HemsConfig';
import { authHeader, handleResponse } from '@/utils/authentication';
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
const config = ref<HemsConfig>();
const binding = ref<{ profile: string; control_mode: HemsDeviceControlMode }>();
const loading = ref(true),
    busy = ref(false),
    error = ref(''),
    notice = ref(''),
    warning = ref(''),
    revision = ref(0);
async function api(path: string, data?: unknown) {
    const body = new FormData();
    if (data) body.append('data', JSON.stringify(data));
    const response = await fetch(path, { headers: authHeader(), ...(data ? { method: 'POST', body } : {}) });
    return handleResponse(response, emitter, router, true);
}
async function load() {
    [config.value, binding.value] = await Promise.all([
        api('/api/hems/config'),
        api('/api/hems/profiles/evse-binding'),
    ]);
}
async function save(path: string, data: unknown) {
    busy.value = true;
    error.value = '';
    notice.value = '';
    warning.value = '';
    try {
        const result = await api(path, data);
        // Config endpoints also report validation/persistence failures in HTTP 200 alerts.
        // An explicit persisted result takes precedence over a best-effort release warning.
        if (result.ok !== true && result.type !== 'success') {
            throw new Error(result.message || t('hemsUi.saveFailed'));
        }
        notice.value = t('hemsUi.saved');
        if (result.type === 'warning' || result.warning) warning.value = result.warning || result.message;
    } catch (e) {
        error.value = message(e);
        busy.value = false;
        return;
    }
    revision.value++;
    try {
        await load();
    } catch (e) {
        warning.value = [warning.value, t('hemsUi.reloadAfterSave') + ' ' + message(e)].filter(Boolean).join(' ');
    }
    busy.value = false;
}
function message(e: unknown): string {
    return e && typeof e === 'object' && 'message' in e ? String(e.message) : String(e || t('hemsUi.requestFailed'));
}
function saveMode() {
    if (config.value) return save('/api/hems/config', { version: config.value.version, mode: config.value.mode });
}
function saveStorage() {
    if (config.value) return save('/api/hems/config', { version: config.value.version, storage: config.value.storage });
}
function saveEvse() {
    if (binding.value) return save('/api/hems/profiles/evse-binding', binding.value);
}
onMounted(async () => {
    try {
        await load();
    } catch (e) {
        error.value = String(e);
    } finally {
        loading.value = false;
    }
});
</script>
