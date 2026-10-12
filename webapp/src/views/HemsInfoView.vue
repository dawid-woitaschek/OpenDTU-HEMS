<template>
    <BasePage :title="$t('hems.Info')" :isLoading="dataLoading" :show-reload="true" @reload="getStatus">
        <div v-if="fetchFailed" class="alert alert-warning" role="alert">{{ $t('hems.StatusUnavailable') }}</div>
        <CardElement :text="$t('hems.Status')" textVariant="text-bg-primary" table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody v-if="hemsStatus.warp">
                        <tr>
                            <th>{{ $t('hems.State') }}</th>
                            <td>{{ $t('hems.' + hemsStatus.state) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.Decision') }}</th>
                            <td>{{ $t('hems.' + hemsStatus.decision) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.ExternalDevices') }}</th>
                            <td>{{ hemsStatus.external_devices || $t('hems.NoneConfigured') }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>
        <CardElement v-if="hemsStatus.warp" :text="$t('hems.Warp')" textVariant="text-bg-primary" table add-space>
            <div class="alert alert-info m-3" role="alert">{{ $t('hems.ReadOnlyHint') }}</div>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody>
                        <tr>
                            <th>{{ $t('hems.Status') }}</th>
                            <td>{{ $t('hems.' + (fetchFailed ? 'Unknown' : hemsStatus.warp.status)) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.Vehicle') }}</th>
                            <td>{{ booleanText(hemsStatus.warp.vehicle_connected) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.Charging') }}</th>
                            <td>{{ booleanText(hemsStatus.warp.charging) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.ChargingState') }}</th>
                            <td>{{ chargingStateText() }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.Power') }}</th>
                            <td>{{ numberText(hemsStatus.warp.power_w, 'W') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.AllowedCurrent') }}</th>
                            <td>{{ numberText(hemsStatus.warp.allowed_current_a, 'A') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.SessionEnergy') }}</th>
                            <td>{{ numberText(hemsStatus.warp.session_energy_kwh, 'kWh') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.MeterAvailable') }}</th>
                            <td>{{ booleanText(hemsStatus.warp.meter_available) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.DataAge') }}</th>
                            <td>
                                {{
                                    numberText(
                                        hemsStatus.warp.age_ms === null ? null : hemsStatus.warp.age_ms / 1000,
                                        's'
                                    )
                                }}
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.LastSuccess') }}</th>
                            <td>
                                {{
                                    numberText(
                                        hemsStatus.warp.last_success_ms === null
                                            ? null
                                            : hemsStatus.warp.last_success_ms / 1000,
                                        's'
                                    )
                                }}
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.LastError') }}</th>
                            <td>
                                {{ $t('hems.Errors.' + hemsStatus.warp.error)
                                }}<span v-if="hemsStatus.warp.modbus_exception">
                                    ({{ hemsStatus.warp.modbus_exception }})</span
                                >
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.PollCounts') }}</th>
                            <td>{{ hemsStatus.warp.successful_polls }} / {{ hemsStatus.warp.failed_polls }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import type { HemsStatus } from '@/types/HemsStatus';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';

export default defineComponent({
    components: { BasePage, CardElement },
    data() {
        return {
            dataLoading: true,
            hemsStatus: {} as HemsStatus,
            timer: undefined as ReturnType<typeof setInterval> | undefined,
            fetching: false,
            fetchFailed: false,
        };
    },
    created() {
        this.getStatus();
        this.timer = setInterval(() => this.getStatus(), 3000);
    },
    beforeUnmount() {
        clearInterval(this.timer);
    },
    methods: {
        booleanText(value: boolean | null) {
            return this.$t('hems.' + (this.fetchFailed || value === null ? 'Unknown' : value ? 'Yes' : 'No'));
        },
        numberText(value: number | null, unit: string) {
            return this.fetchFailed || value === null
                ? this.$t('hems.Unknown')
                : value.toLocaleString(undefined, { maximumFractionDigits: 3 }) + ' ' + unit;
        },
        chargingStateText() {
            const state = this.hemsStatus.warp.charging_state;
            return this.fetchFailed || state === null
                ? this.$t('hems.Unknown')
                : this.$t('hems.ChargingStates.' + state);
        },
        getStatus() {
            if (this.fetching) return;
            this.fetching = true;
            fetch('/api/hems/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: HemsStatus) => {
                    this.hemsStatus = data;
                    this.dataLoading = false;
                    this.fetchFailed = false;
                })
                .catch(() => {
                    this.fetchFailed = true;
                    this.dataLoading = false;
                })
                .finally(() => {
                    this.fetching = false;
                });
        },
    },
});
</script>
