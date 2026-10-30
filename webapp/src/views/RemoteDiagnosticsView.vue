<template>
    <BasePage :title="$t('remoteDiagnostics.title')" :isLoading="loading">
        <div v-if="message" class="alert" :class="failed ? 'alert-danger' : 'alert-success'" role="status">
            {{ message }}
        </div>
        <p>{{ $t('remoteDiagnostics.help') }}</p>
        <form @submit.prevent="save">
            <CardElement :text="$t('remoteDiagnostics.configuration')" textVariant="text-bg-primary">
                <div class="form-check mb-3">
                    <input id="remote-enabled" v-model="config.enabled" class="form-check-input" type="checkbox" />
                    <label for="remote-enabled" class="form-check-label">{{ $t('remoteDiagnostics.enable') }}</label>
                </div>
                <div class="mb-3">
                    <label for="remote-endpoint" class="form-label">{{ $t('remoteDiagnostics.endpoint') }}</label>
                    <input
                        id="remote-endpoint"
                        v-model="config.endpoint"
                        class="form-control"
                        type="url"
                        maxlength="256"
                        placeholder="https://collector.example.org/api/v1/telemetry"
                        :required="config.enabled"
                    />
                </div>
                <div class="mb-3">
                    <label for="remote-token" class="form-label">{{ $t('remoteDiagnostics.token') }}</label>
                    <input
                        id="remote-token"
                        v-model="token"
                        class="form-control"
                        type="password"
                        maxlength="128"
                        autocomplete="new-password"
                        :disabled="clearToken"
                        aria-describedby="remote-token-help"
                    />
                    <div id="remote-token-help" class="form-text">
                        {{
                            $t(
                                config.token_set
                                    ? 'remoteDiagnostics.tokenConfigured'
                                    : 'remoteDiagnostics.tokenMissing'
                            )
                        }}
                        {{ $t('remoteDiagnostics.tokenHelp') }}
                    </div>
                    <div class="form-check mt-2">
                        <input id="remote-clear" v-model="clearToken" type="checkbox" class="form-check-input" />
                        <label for="remote-clear" class="form-check-label">{{
                            $t('remoteDiagnostics.clearToken')
                        }}</label>
                    </div>
                </div>
                <div class="mb-3">
                    <label for="remote-interval" class="form-label">{{ $t('remoteDiagnostics.interval') }}</label>
                    <input
                        id="remote-interval"
                        v-model.number="config.interval_minutes"
                        class="form-control"
                        type="number"
                        min="1"
                        max="1440"
                        step="1"
                        required
                    />
                </div>
                <button class="btn btn-primary" type="submit" :disabled="saving">
                    {{ $t('remoteDiagnostics.save') }}
                </button>
            </CardElement>
        </form>
        <CardElement v-if="status" :text="$t('remoteDiagnostics.status')" textVariant="text-bg-primary" class="mt-3">
            <dl class="row mb-0">
                <template v-for="row in statusRows" :key="row.key">
                    <dt class="col-sm-6">{{ $t('remoteDiagnostics.' + row.key) }}</dt>
                    <dd class="col-sm-6 text-break">{{ row.value }}</dd>
                </template>
            </dl>
            <p class="text-body-secondary">{{ $t('remoteDiagnostics.uptimeHelp') }}</p>
            <button
                class="btn btn-outline-primary"
                type="button"
                :disabled="!status.enabled || status.busy || sending"
                @click="sendNow"
            >
                {{ $t('remoteDiagnostics.sendNow') }}
            </button>
        </CardElement>
    </BasePage>
</template>
<script lang="ts">
import { defineComponent } from 'vue';
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import {
    remoteDiagnosticsWrite,
    type RemoteDiagnosticsConfig,
    type RemoteDiagnosticsStatus,
} from '@/types/RemoteDiagnostics';

export default defineComponent({
    components: { BasePage, CardElement },
    data() {
        return {
            loading: true,
            saving: false,
            sending: false,
            failed: false,
            message: '',
            config: { enabled: false, endpoint: '', interval_minutes: 60, token_set: false } as RemoteDiagnosticsConfig,
            token: '',
            clearToken: false,
            status: null as RemoteDiagnosticsStatus | null,
            timer: undefined as ReturnType<typeof setInterval> | undefined,
        };
    },
    computed: {
        statusRows() {
            if (!this.status) return [];
            const status = this.status;
            const keys = [
                'enabled',
                'token_set',
                'busy',
                'send_requested',
                'runtime_reason',
                'last_attempt_uptime_ms',
                'last_success_uptime_ms',
                'last_http_result',
                'last_error',
                'next_upload_in_seconds',
                'telemetry_sequence',
                'retry_pending',
                'last_acknowledged_checkpoint_sequence',
                'oldest_available_sequence',
                'newest_sequence',
                'pending_samples',
                'dropped_samples',
                'pending_allocation_failures',
                'dropped_allocation_failures',
                'spool_available',
                'payload_bytes',
            ] as const;
            return keys.map((key) => {
                const value = status[key];
                return {
                    key,
                    value:
                        typeof value === 'boolean'
                            ? this.$t(value ? 'remoteDiagnostics.yes' : 'remoteDiagnostics.no')
                            : (value ?? '—'),
                };
            });
        },
    },
    async mounted() {
        try {
            await this.loadConfiguration();
            await this.refreshStatus();
        } catch {
            this.showError();
        } finally {
            this.loading = false;
        }
        this.timer = setInterval(() => {
            void this.refreshStatus().catch(() => this.showError());
        }, 10000);
    },
    beforeUnmount() {
        clearInterval(this.timer);
    },
    methods: {
        async read(path: string, options: RequestInit = {}) {
            const response = await fetch('/api/remote-diagnostics/' + path, { ...options, headers: authHeader() });
            return handleResponse(response, this.$emitter, this.$router, true);
        },
        async loadConfiguration() {
            this.config = await this.read('config');
        },
        async refreshStatus() {
            this.status = await this.read('status');
        },
        showError() {
            this.failed = true;
            this.message = this.$t('remoteDiagnostics.failed');
        },
        async save() {
            this.saving = true;
            this.message = '';
            try {
                const data = new FormData();
                data.append('data', JSON.stringify(remoteDiagnosticsWrite(this.config, this.token, this.clearToken)));
                const result = await this.read('config', { method: 'POST', body: data });
                if (!result.success) throw new Error('configuration_rejected');
                this.token = '';
                this.clearToken = false;
                await this.loadConfiguration();
                await this.refreshStatus();
                this.failed = false;
                this.message = this.$t('remoteDiagnostics.saved');
            } catch {
                this.showError();
            } finally {
                this.saving = false;
            }
        },
        async sendNow() {
            this.sending = true;
            this.message = '';
            try {
                const result = await this.read('send', { method: 'POST' });
                if (!result.scheduled) throw new Error('send_not_scheduled');
                await this.refreshStatus();
                this.failed = false;
                this.message = this.$t('remoteDiagnostics.scheduled');
            } catch {
                this.showError();
            } finally {
                this.sending = false;
            }
        },
    },
});
</script>
