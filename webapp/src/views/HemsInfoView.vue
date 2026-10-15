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

        <CardElement
            v-if="hemsStatus.normalized"
            :text="$t('hems.NormalizedState')"
            textVariant="text-bg-primary"
            table
            add-space
        >
            <div class="alert alert-info m-3" role="alert">{{ $t('hems.NormalizedHint') }}</div>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody>
                        <tr>
                            <th>{{ $t('hems.GridPower') }}</th>
                            <td>
                                {{ signalNumberText(hemsStatus.normalized.grid.power_w, 'W') }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.grid.power_w)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.grid.power_w) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.PvPower') }}</th>
                            <td>
                                {{ signalNumberText(hemsStatus.normalized.pv.power_w, 'W') }}
                                <span class="badge ms-2" :class="signalQualityClass(hemsStatus.normalized.pv.power_w)">
                                    {{ signalQualityText(hemsStatus.normalized.pv.power_w) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.StorageSoc') }}</th>
                            <td>
                                {{ signalNumberText(hemsStatus.normalized.storage.soc_pct, '%') }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.storage.soc_pct)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.storage.soc_pct) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.AcPower') }}</th>
                            <td>
                                {{ signalNumberText(hemsStatus.normalized.storage.ac_power_w, 'W') }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.storage.ac_power_w)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.storage.ac_power_w) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.Charging') }}</th>
                            <td>
                                {{ signalBooleanText(hemsStatus.normalized.storage.charging) }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.storage.charging)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.storage.charging) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.Discharging') }}</th>
                            <td>
                                {{ signalBooleanText(hemsStatus.normalized.storage.discharging) }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.storage.discharging)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.storage.discharging) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.EvseVehicle') }}</th>
                            <td>
                                {{ signalBooleanText(hemsStatus.normalized.evse.vehicle_connected) }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.evse.vehicle_connected)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.evse.vehicle_connected) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.Charging') }}</th>
                            <td>
                                {{ signalBooleanText(hemsStatus.normalized.evse.charging) }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.evse.charging)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.evse.charging) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.Power') }}</th>
                            <td>
                                {{ signalNumberText(hemsStatus.normalized.evse.power_w, 'W') }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.evse.power_w)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.evse.power_w) }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.AllowedCurrent') }}</th>
                            <td>
                                {{ signalNumberText(hemsStatus.normalized.evse.allowed_current_a, 'A') }}
                                <span
                                    class="badge ms-2"
                                    :class="signalQualityClass(hemsStatus.normalized.evse.allowed_current_a)"
                                >
                                    {{ signalQualityText(hemsStatus.normalized.evse.allowed_current_a) }}
                                </span>
                            </td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>

        <CardElement
            v-if="hemsStatus.constraints"
            :text="$t('hems.Constraints')"
            textVariant="text-bg-primary"
            table
            add-space
        >
            <div class="alert alert-info m-3" role="alert">{{ $t('hems.ConstraintsHint') }}</div>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody>
                        <tr>
                            <th>{{ $t('hems.StorageChargeAllowed') }}</th>
                            <td>{{ booleanText(hemsStatus.constraints.storage.charge_allowed) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.StorageDischargeAllowed') }}</th>
                            <td>{{ booleanText(hemsStatus.constraints.storage.discharge_allowed) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.MaxChargePower') }}</th>
                            <td>{{ limitText(hemsStatus.constraints.storage.max_charge_power) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.MaxDischargePower') }}</th>
                            <td>{{ limitText(hemsStatus.constraints.storage.max_discharge_power) }}</td>
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

        <CardElement
            v-if="hemsStatus.storage"
            :text="$t('storage.Title')"
            textVariant="text-bg-primary"
            table
            add-space
        >
            <div class="alert alert-info m-3" role="alert">{{ $t('hems.ReadOnlyHint') }}</div>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody>
                        <tr>
                            <th>{{ $t('hems.Status') }}</th>
                            <td>{{ $t('hems.' + (fetchFailed ? 'Unknown' : hemsStatus.storage.status)) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.Profile') }}</th>
                            <td>{{ hemsStatus.storage.profile || $t('hems.Unknown') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.Host') }}</th>
                            <td>{{ hemsStatus.storage.host || $t('hems.Unknown') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.ModbusEndpoint') }}</th>
                            <td>{{ endpointText() }}</td>
                        </tr>
                        <tr>
                            <th>SoC</th>
                            <td>{{ numberText(hemsStatus.storage.soc_pct, '%') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.AcPower') }}</th>
                            <td>{{ numberText(hemsStatus.storage.ac_power_w, 'W') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.DcPower') }}</th>
                            <td>{{ numberText(hemsStatus.storage.dc_power_w, 'W') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.BatteryVoltage') }}</th>
                            <td>{{ numberText(hemsStatus.storage.battery_voltage_v, 'V') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.BatteryCurrent') }}</th>
                            <td>{{ numberText(hemsStatus.storage.battery_current_a, 'A') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.Temperature') }}</th>
                            <td>{{ numberText(hemsStatus.storage.temperature_c, '°C') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.Charging') }}</th>
                            <td>{{ booleanText(hemsStatus.storage.charging) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.Discharging') }}</th>
                            <td>{{ booleanText(hemsStatus.storage.discharging) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.ChargeEnergyTotal') }}</th>
                            <td>{{ numberText(hemsStatus.storage.charge_energy_total_kwh, 'kWh') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.DischargeEnergyTotal') }}</th>
                            <td>{{ numberText(hemsStatus.storage.discharge_energy_total_kwh, 'kWh') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.OperatingState') }}</th>
                            <td>{{ numberText(hemsStatus.storage.operating_state, '') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.FaultCode') }}</th>
                            <td>{{ numberText(hemsStatus.storage.fault_code, '') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.AlarmCode') }}</th>
                            <td>{{ numberText(hemsStatus.storage.alarm_code, '') }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.FaultActive') }}</th>
                            <td>{{ booleanText(hemsStatus.storage.fault_active) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('storage.AlarmActive') }}</th>
                            <td>{{ booleanText(hemsStatus.storage.alarm_active) }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.DataAge') }}</th>
                            <td>
                                {{
                                    numberText(
                                        hemsStatus.storage.age_ms === null ? null : hemsStatus.storage.age_ms / 1000,
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
                                        hemsStatus.storage.last_success_ms === null
                                            ? null
                                            : hemsStatus.storage.last_success_ms / 1000,
                                        's'
                                    )
                                }}
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.LastError') }}</th>
                            <td>
                                {{ $t('hems.Errors.' + hemsStatus.storage.error)
                                }}<span v-if="hemsStatus.storage.modbus_exception">
                                    ({{ hemsStatus.storage.modbus_exception }})</span
                                >
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('hems.PollCounts') }}</th>
                            <td>{{ hemsStatus.storage.successful_polls }} / {{ hemsStatus.storage.failed_polls }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>
        <CardElement v-if="hemsStatus.actuation" text="Device control" textVariant="text-bg-primary" add-space>
            <div v-for="{ target, name } in actuationTargets" :key="name" class="mb-3">
                <strong>{{ name === 'storage' ? 'Storage' : 'EVSE' }}</strong>
                <p>
                    Write mode: {{ target.control_mode === 'hems' ? 'HEMS control' : 'Monitor only' }} · Profile:
                    {{ target.profile || 'No imported profile bound' }}
                </p>
                <p>Capability: {{ controlText(target.profile_info.control_reason) }}</p>
                <p v-for="reason in target.blockers" :key="reason">{{ controlText(reason) }}</p>
            </div>
            <RouterLink to="/hems/settings">HEMS settings and manual semantic controls</RouterLink>
        </CardElement>
    </BasePage>
</template>

<script lang="ts">
import { controlText } from '@/hemsControl';
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import type { HemsActuationTarget, HemsPowerLimit, HemsQuality, HemsSignal, HemsStatus } from '@/types/HemsStatus';
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
    computed: {
        actuationTargets(): { name: string; target: HemsActuationTarget }[] {
            const actuation = this.hemsStatus.actuation;
            return actuation
                ? [
                      { name: 'evse', target: actuation.evse },
                      { name: 'storage', target: actuation.storage },
                  ]
                : [];
        },
    },
    methods: {
        controlText,
        booleanText(value: boolean | null) {
            return this.$t('hems.' + (this.fetchFailed || value === null ? 'Unknown' : value ? 'Yes' : 'No'));
        },
        numberText(value: number | null, unit: string) {
            if (this.fetchFailed || value === null) return this.$t('hems.Unknown');
            const number = value.toLocaleString(undefined, { maximumFractionDigits: 3 });
            return unit ? number + ' ' + unit : number;
        },
        signalNumberText(signal: HemsSignal<number>, unit: string) {
            return this.numberText(signal.value, unit);
        },
        signalBooleanText(signal: HemsSignal<boolean>) {
            return this.booleanText(signal.value);
        },
        displayedQuality(quality: HemsQuality): HemsQuality {
            return this.fetchFailed ? 'unknown' : quality;
        },
        signalQualityText(signal: HemsSignal<unknown>) {
            const quality = this.displayedQuality(signal.quality);
            return this.$t('hems.Quality' + quality.charAt(0).toUpperCase() + quality.slice(1));
        },
        signalQualityClass(signal: HemsSignal<unknown>) {
            switch (this.displayedQuality(signal.quality)) {
                case 'fresh':
                    return 'text-bg-success';
                case 'stale':
                    return 'text-bg-warning';
                default:
                    return 'text-bg-secondary';
            }
        },
        limitText(limit: HemsPowerLimit) {
            if (this.fetchFailed) return this.$t('hems.Unknown');
            if (!limit.active) return this.$t('hems.NoLimit');
            return this.numberText(limit.value_w, 'W');
        },
        endpointText() {
            if (this.fetchFailed || !this.hemsStatus.storage.port || !this.hemsStatus.storage.unit) {
                return this.$t('hems.Unknown');
            }
            return `${this.hemsStatus.storage.port} / Unit ${this.hemsStatus.storage.unit}`;
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
