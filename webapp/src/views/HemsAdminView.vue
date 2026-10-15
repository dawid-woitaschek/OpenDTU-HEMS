<template>
    <BasePage :title="$t('hems.Settings')" :isLoading="loading">
        <p v-if="error" class="alert alert-danger" role="alert">{{ error }}</p>
        <p v-if="notice" class="alert alert-success" role="status">{{ notice }}</p>
        <template v-if="config">
            <CardElement text="HEMS mode" textVariant="text-bg-primary">
                <form @submit.prevent="saveMode">
                    <label for="hems-mode" class="form-label">Global HEMS mode</label>
                    <select id="hems-mode" v-model="config.mode" class="form-select mb-2">
                        <option>Disabled</option>
                        <option>Monitor</option>
                        <option>Active</option>
                    </select>
                    <p>
                        Monitor calculates policies and permits manual semantic controls. Active also permits automatic
                        policy writes to enabled device types. Disabled stops HEMS polling and automatic control.
                    </p>
                    <button class="btn btn-primary" :disabled="busy">Save mode</button>
                </form>
            </CardElement>
            <CardElement text="Device write permissions" textVariant="text-bg-primary" add-space>
                <p>
                    Monitor only: telemetry is used; HEMS never issues normal commands to this device type. Switching
                    from HEMS control attempts release once.
                </p>
                <p>HEMS control: manual controls and Active policies may write semantic commands.</p>
                <form class="mb-3" @submit.prevent="saveStorage">
                    <label for="storage-control" class="form-label fw-bold">Storage control</label>
                    <select id="storage-control" v-model="config.storage.control_mode" class="form-select mb-2">
                        <option value="monitor">Monitor only</option>
                        <option value="hems">HEMS control</option>
                    </select>
                    <button class="btn btn-primary" :disabled="busy">Save Storage control</button>
                </form>
                <form v-if="binding" @submit.prevent="saveEvse">
                    <label for="evse-control" class="form-label fw-bold">EVSE control</label>
                    <select id="evse-control" v-model="binding.control_mode" class="form-select mb-2">
                        <option value="monitor">Monitor only</option>
                        <option value="hems">HEMS control</option>
                    </select>
                    <button class="btn btn-primary" :disabled="busy">Save EVSE control</button>
                </form>
                <RouterLink to="/hems/devices">Configure endpoints and import or replace device profiles</RouterLink>
            </CardElement>
            <HemsControlPanel :key="revision" />
        </template>
    </BasePage>
</template>
<script setup lang="ts">
import { onMounted, ref } from 'vue';
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import HemsControlPanel from '@/components/HemsControlPanel.vue';
import type { HemsConfig, HemsDeviceControlMode } from '@/types/HemsConfig';
import { authHeader } from '@/utils/authentication';
const config = ref<HemsConfig>();
const binding = ref<{ profile: string; control_mode: HemsDeviceControlMode }>();
const loading = ref(true),
    busy = ref(false),
    error = ref(''),
    notice = ref(''),
    revision = ref(0);
async function api(path: string, data?: unknown) {
    const body = new FormData();
    if (data) body.append('data', JSON.stringify(data));
    const response = await fetch(path, { headers: authHeader(), ...(data ? { method: 'POST', body } : {}) });
    const result = await response.json();
    if (!response.ok || result.type === 'warning' || result.type === 'danger' || result.error || result.ok === false)
        throw new Error(result.message || 'HEMS request failed');
    return result;
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
    try {
        await api(path, data);
        await load();
        revision.value++;
        notice.value = 'Settings saved.';
    } catch (e) {
        error.value = String(e);
    } finally {
        busy.value = false;
    }
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
