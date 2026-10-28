<template>
    <NavBar />
    <div id="main-content" class="app-content" tabindex="-1"><router-view /></div>
</template>
<script setup lang="ts">
import NavBar from './components/NavBar.vue';
import { getCurrentInstance, onMounted, onUnmounted } from 'vue';
import { useRouter } from 'vue-router';
import { startLiveData, stopLiveData } from '@/utils/liveData';
import { authUrl } from '@/utils/authentication';
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
let authKey: string | undefined;
const start = () => {
    const nextAuth = authUrl();
    if (nextAuth === authKey) return;
    authKey = nextAuth;
    startLiveData(emitter, router);
};
onMounted(start);
emitter.on('logged-in', start);
emitter.on('logged-out', start);
onUnmounted(() => {
    emitter.off('logged-in', start);
    emitter.off('logged-out', start);
    stopLiveData();
});
</script>
