<template>
    <CardElement :text="$t('hems.ValidationTitle')" textVariant="text-bg-warning" add-space>
        <div class="p-3">
            <p>{{ $t('hems.ValidationWarning') }}</p>
            <p>{{ $t('hems.ValidationHint') }}</p>
            <div v-if="status">
                <strong>{{ status.state }}</strong> · {{ remainingSeconds }} s
                <div v-for="name in targets" :key="name" class="my-2">
                    <strong>{{ name }}</strong> · {{ status[name].profile || '—' }}
                    <div>{{ status[name].validation.blockers.join(', ') || '—' }}</div>
                    <div>{{ status[name].recovery.state }} / {{ status[name].recovery.reason }}</div>
                    <div>{{ status[name].command.state }} / {{ status[name].command.last_error }}</div>
                    <div>{{ status[name].cleanup.state }} / {{ status[name].cleanup.last_error }}</div>
                </div>
            </div>
            <div v-if="error" class="alert alert-danger" role="alert">{{ error }}</div>
            <button class="btn btn-warning me-2" :disabled="busy || !status || status.active" @click="start">
                {{ $t('hems.ValidationStart') }}
            </button>
            <button class="btn btn-secondary" :disabled="busy || !token || !status?.active" @click="revoke">
                {{ $t('hems.ValidationRevoke') }}
            </button>
        </div>
    </CardElement>
</template>
<script lang="ts">
import CardElement from '@/components/CardElement.vue';
import type { HemsActuationTarget, HemsStatus } from '@/types/HemsStatus';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';
type Status = HemsStatus['validation'] & { evse: HemsActuationTarget; storage: HemsActuationTarget };
export default defineComponent({
    components: { CardElement },
    data() {
        return {
            status: null as Status | null,
            targets: ['evse', 'storage'] as const,
            token: '', // Memory only; reload loses it and the server TTL still applies.
            error: '',
            busy: false,
            fetching: false,
            now: Date.now(),
            deadline: 0,
            ticks: 0,
            timer: undefined as ReturnType<typeof setInterval> | undefined,
        };
    },
    computed: {
        remainingSeconds(): number {
            return Math.max(0, Math.ceil((this.deadline - this.now) / 1000));
        },
    },
    created() {
        this.refresh();
        this.timer = setInterval(() => {
            this.now = Date.now();
            if (++this.ticks % 3 === 0) this.refresh();
        }, 1000);
    },
    beforeUnmount() {
        clearInterval(this.timer);
        if (this.token) {
            fetch('/api/hems/validation/session', {
                method: 'DELETE',
                headers: { ...authHeader(), 'X-HEMS-Validation-Token': this.token },
            }).catch(() => {}); // Server expiry remains authoritative if navigation drops the request.
        }
    },
    methods: {
        async refresh() {
            if (this.fetching) return;
            this.fetching = true;
            try {
                const response = await fetch('/api/hems/validation/status', { headers: authHeader() });
                this.status = (await handleResponse(response, this.$emitter, this.$router)) as Status;
                this.deadline = Date.now() + this.status.remaining_ms;
            } catch {
                this.error = this.$t('hems.StatusUnavailable');
                this.status = null;
            } finally {
                this.fetching = false;
            }
        },
        async start() {
            this.busy = true;
            this.error = '';
            try {
                const response = await fetch('/api/hems/validation/session', { method: 'POST', headers: authHeader() });
                const data = await handleResponse(response, this.$emitter, this.$router);
                if (!data.accepted || !data.token) throw new Error();
                this.token = data.token;
                await this.refresh();
            } catch {
                this.error = this.$t('hems.ValidationRejected');
            } finally {
                this.busy = false;
            }
        },
        async revoke() {
            this.busy = true;
            try {
                const response = await fetch('/api/hems/validation/session', {
                    method: 'DELETE',
                    headers: { ...authHeader(), 'X-HEMS-Validation-Token': this.token },
                });
                await handleResponse(response, this.$emitter, this.$router);
                this.token = '';
                await this.refresh();
            } catch {
                this.error = this.$t('hems.ValidationRejected');
            } finally {
                this.busy = false;
            }
        },
    },
});
</script>
