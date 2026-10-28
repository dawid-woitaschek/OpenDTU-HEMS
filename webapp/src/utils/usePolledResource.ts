import { getCurrentInstance, onMounted, onUnmounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import { authHeader, handleResponse } from '@/utils/authentication';

// One source per mounted consumer. Never present retained data as current after an error.
export function usePolledResource<T>(path: string, enabled: () => boolean = () => true) {
    const data = ref<T>();
    const unavailable = ref(false);
    const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
    const router = useRouter();
    const controller = new AbortController();
    let timer: ReturnType<typeof setTimeout> | undefined;
    async function poll() {
        if (!enabled()) {
            timer = setTimeout(poll, 3000);
            return;
        }
        try {
            const response = await fetch(path, { headers: authHeader(), signal: controller.signal });
            const result = await handleResponse(response, emitter, router);
            if (!controller.signal.aborted) {
                data.value = result;
                unavailable.value = false;
            }
        } catch {
            unavailable.value = true;
        } finally {
            if (!controller.signal.aborted) timer = setTimeout(poll, 3000);
        }
    }
    onMounted(poll);
    onUnmounted(() => {
        controller.abort();
        clearTimeout(timer);
    });
    return { data, unavailable };
}
