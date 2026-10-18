<template>
    <SupportSnapshot />
    <CardElement :text="$t('hemsUi.systemDiagnostics')" textVariant="text-bg-primary" add-space>
        <RouterLink to="/info/system">{{ $t('hemsUi.systemStatus') }}</RouterLink>
        <p v-if="error" class="alert alert-warning mt-2" role="alert">{{ $t('hemsUi.loadFailed') }}</p>
        <details class="mt-3" @toggle="load">
            <summary>{{ $t('hemsUi.resetDiagnostics') }}</summary>
            <pre v-if="data" class="small mt-3">{{ JSON.stringify(data, null, 2) }}</pre>
        </details>
    </CardElement>
</template>
<script setup lang="ts">
import { getCurrentInstance, ref } from 'vue';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import SupportSnapshot from '@/components/hems/SupportSnapshot.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
const data = ref<unknown>(),
    error = ref(false);
let busy = false;
async function load(event: Event) {
    if (!(event.target as HTMLDetailsElement).open || busy) return;
    busy = true;
    try {
        data.value = await fetch('/api/system/status', { headers: authHeader() }).then((r) =>
            handleResponse(r, emitter, router)
        );
        error.value = false;
    } catch {
        error.value = true;
    } finally {
        busy = false;
    }
}
</script>
<style scoped>
pre {
    max-height: 36rem;
    overflow: auto;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
}
</style>
