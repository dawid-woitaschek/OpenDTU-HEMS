<template>
    <p v-if="error" class="alert alert-danger" role="alert">{{ error }}</p>
    <p v-if="status?.state === 'Active'" class="alert alert-info">
        Automation is Active and may override manual commands. Use Monitor for hardware testing.
    </p>
    <p v-if="notice" role="status">{{ notice }}</p>
    <template v-if="status">
        <CardElement
            v-for="name in targets"
            :key="name"
            :text="name === 'storage' ? 'Storage' : 'EVSE / WARP'"
            textVariant="text-bg-primary"
            add-space
        >
            <dl class="row">
                <dt class="col-sm-4">Write mode</dt>
                <dd class="col-sm-8">{{ target(name).control_mode === 'hems' ? 'HEMS control' : 'Monitor only' }}</dd>
                <dt class="col-sm-4">Reachability</dt>
                <dd class="col-sm-8">{{ telemetry(name).reachable ? 'Reachable' : 'Unreachable' }}</dd>
                <dt class="col-sm-4">Telemetry freshness</dt>
                <dd class="col-sm-8">
                    {{ telemetry(name).valid && !telemetry(name).stale ? 'Fresh' : 'Waiting for fresh telemetry' }} ·
                    {{ age(telemetry(name).age_ms) }}
                </dd>
                <dt class="col-sm-4">Profile</dt>
                <dd class="col-sm-8">{{ target(name).profile || 'No imported profile bound' }}</dd>
                <dt class="col-sm-4">Control capability</dt>
                <dd class="col-sm-8">{{ controlText(target(name).profile_info.control_reason) }}</dd>
                <template v-if="name === 'storage'">
                    <dt class="col-sm-4">Current constraint</dt>
                    <dd class="col-sm-8">
                        {{
                            status.constraints.storage.discharge_allowed
                                ? 'Discharge permitted'
                                : 'EV protection blocks discharge (semantic stop)'
                        }}
                    </dd>
                    <dt class="col-sm-4">EV protection proposal</dt>
                    <dd class="col-sm-8">
                        {{
                            status.shadow.ev_protection.would_block_storage_discharge
                                ? 'Would block storage discharge'
                                : 'No discharge restriction'
                        }}{{
                            status.shadow.ev_protection.state === 'waiting'
                                ? ' — waiting for 120 seconds of fresh not-charging telemetry'
                                : ''
                        }}
                    </dd>
                </template>
                <template v-else
                    ><dt class="col-sm-4">Current constraints</dt>
                    <dd class="col-sm-8">No automatic EVSE policy installed</dd></template
                >
                <dt class="col-sm-4">Command state</dt>
                <dd class="col-sm-8">
                    {{ target(name).command.id ? controlText(target(name).command.state) : 'No command submitted' }}
                </dd>
            </dl>
            <p v-for="reason in target(name).blockers" :key="reason" class="text-secondary">
                {{ controlText(reason) }}
            </p>
            <h3 class="h5">Manual semantic controls</h3>
            <p v-if="!target(name).profile_info.operations.length">
                {{ controlText(target(name).profile_info.control_reason) }}
            </p>
            <form
                v-for="op in target(name).profile_info.operations"
                :key="op.action"
                class="d-flex flex-wrap align-items-center gap-2 mb-2"
                @submit.prevent="send(name, op)"
            >
                <label :for="`${name}-${op.action}`" class="mb-0">{{ controlText(op.action) }}</label>
                <input
                    v-if="op.has_input"
                    :id="`${name}-${op.action}`"
                    class="form-control w-auto"
                    type="number"
                    :min="op.min"
                    :max="op.max"
                    :step="op.step"
                    :value="values[`${name}-${op.action}`] ?? op.min"
                    :disabled="!canSend(name, op)"
                    required
                    @input="values[`${name}-${op.action}`] = Number(($event.target as HTMLInputElement).value)"
                />
                <span v-if="op.has_input">{{ op.unit }} ({{ op.min }}–{{ op.max }}, step {{ op.step }})</span>
                <button class="btn btn-primary" :disabled="!canSend(name, op)">
                    {{ op.has_input ? 'Apply' : controlText(op.action) }}
                </button>
            </form>
            <p v-if="name === 'storage' && !status.constraints.storage.discharge_allowed">
                Discharge commands are unavailable while Active EV protection applies.
            </p>
            <div v-if="target(name).command.id" class="border rounded p-3 mt-3" role="status">
                <h3 class="h5">Last command result</h3>
                <p>
                    Requested: {{ controlText(target(name).command.action ?? '') }}
                    {{ target(name).command.value ?? '' }} {{ commandUnit(name) }} ·
                    {{ target(name).command.automatic ? 'Automatic policy' : 'Manual' }}
                </p>
                <template v-if="finished(name)">
                    <p>
                        Transport: {{ controlText(target(name).command.transport_error)
                        }}<span v-if="target(name).command.modbus_exception">
                            · Modbus exception {{ target(name).command.modbus_exception }}</span
                        >
                    </p>
                    <p>
                        Write / readback:
                        {{
                            target(name).command.readback_verified
                                ? 'Modbus write verified'
                                : target(name).command.write_attempted
                                  ? 'Not verified — ' + controlText(target(name).command.execution_error)
                                  : 'No write attempted'
                        }}
                    </p>
                    <p v-if="target(name).command.state === 'failed'">
                        Command error: {{ controlText(target(name).command.last_error) }} ·
                        {{ controlText(target(name).command.planning_error) }} ·
                        {{ controlText(target(name).command.execution_error) }}
                    </p>
                    <p>
                        Completed at device uptime {{ target(name).command.completed_ms }} ms ·
                        {{ age(target(name).command.age_ms) }}
                    </p>
                    <p>
                        {{
                            afterCommand(name)
                                ? 'Fresh telemetry after command'
                                : 'Waiting for fresh telemetry after command'
                        }}
                        · telemetry age {{ age(telemetry(name).age_ms) }}
                    </p>
                    <p v-if="name === 'storage'">
                        Observed AC power: {{ status.storage.ac_power_w ?? 'Unknown' }} W · SoC:
                        {{ status.storage.soc_pct ?? 'Unknown' }} %
                    </p>
                    <p v-else>
                        Allowed current telemetry: {{ status.warp.allowed_current_a ?? 'Unknown' }} A · Charging
                        observed: {{ status.warp.charging === null ? 'Unknown' : status.warp.charging ? 'Yes' : 'No' }}
                    </p>
                    <p class="small text-secondary">
                        Register readback verifies the Modbus value. Physical behavior must be assessed from fresh
                        telemetry and the device.
                    </p>
                </template>
            </div>
            <details class="mt-3">
                <summary>Advanced diagnostics and runtime profile</summary>
                <dl class="row mt-2">
                    <dt class="col-sm-4">Schema version</dt>
                    <dd class="col-sm-8">{{ target(name).profile_info.schema_version || 'No profile' }}</dd>
                    <dt class="col-sm-4">Telemetry / control supported</dt>
                    <dd class="col-sm-8">
                        {{ yes(target(name).profile_info.telemetry_supported) }} /
                        {{ yes(target(name).profile_info.control_supported) }}
                    </dd>
                    <dt class="col-sm-4">Identity / recovery metadata</dt>
                    <dd class="col-sm-8">
                        {{ yes(target(name).profile_info.identity_metadata) }} /
                        {{ yes(target(name).profile_info.recovery_metadata) }} (diagnostic only)
                    </dd>
                    <dt class="col-sm-4">Lifecycle metadata</dt>
                    <dd class="col-sm-8">No watchdog lifecycle declared</dd>
                    <dt class="col-sm-4">Profile revision / fingerprint</dt>
                    <dd class="col-sm-8">
                        {{ target(name).profile_info.source_revision || 'Not provided' }} /
                        {{ target(name).profile_info.fingerprint }}
                    </dd>
                    <dt class="col-sm-4">Command generation</dt>
                    <dd class="col-sm-8">{{ target(name).command.generation ?? 'No command yet' }}</dd>
                    <dt class="col-sm-4">Last telemetry transport error</dt>
                    <dd class="col-sm-8">{{ controlText(telemetry(name).error) }}</dd>
                    <dt class="col-sm-4">Best-effort release</dt>
                    <dd class="col-sm-8">
                        {{
                            target(name).cleanup.attempts
                                ? controlText(target(name).cleanup.state) +
                                  ' · ' +
                                  controlText(target(name).cleanup.transport_error)
                                : 'No cleanup requested'
                        }}
                    </dd>
                </dl>
                <p>
                    Old #22 journal files are ignored. An administrator can inspect or delete hems-recovery-* files in
                    File Management.
                </p>
            </details>
        </CardElement>
    </template>
</template>
<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue';
import CardElement from '@/components/CardElement.vue';
import type { HemsStatus, HemsOperation } from '@/types/HemsStatus';
import { authHeader } from '@/utils/authentication';
import { controlText } from '@/hemsControl';
type Target = 'storage' | 'evse';
const targets: Target[] = ['storage', 'evse'];
const status = ref<HemsStatus>(),
    values = ref<Record<string, number>>({}),
    error = ref(''),
    notice = ref(''),
    busy = ref(false);
let timer: ReturnType<typeof setTimeout> | undefined,
    disposed = false;
function target(name: Target) {
    return status.value!.actuation[name];
}
function telemetry(name: Target) {
    return name === 'storage' ? status.value!.storage : status.value!.warp;
}
function age(ms: number | null) {
    return ms === null ? 'No update yet' : ms < 1000 ? `${ms} ms ago` : `${(ms / 1000).toFixed(1)} s ago`;
}
function yes(value: boolean) {
    return value ? 'Yes' : 'No';
}
function finished(name: Target) {
    return ['succeeded', 'failed'].includes(target(name).command.state);
}
function afterCommand(name: Target) {
    const t = telemetry(name),
        command = target(name).command;
    return (
        t.valid &&
        !t.stale &&
        t.last_success_ms !== null &&
        (t.last_success_ms - command.completed_ms) >>> 0 < 0x80000000
    );
}
function commandUnit(name: Target) {
    return target(name).profile_info.operations.find((op) => op.action === target(name).command.action)?.unit ?? '';
}
function canSend(name: Target, op: HemsOperation) {
    return (
        !busy.value &&
        !error.value &&
        target(name).allowed &&
        !(
            name === 'storage' &&
            op.action === 'set_discharge_power_w' &&
            !status.value!.constraints.storage.discharge_allowed
        )
    );
}
async function refresh() {
    const response = await fetch('/api/hems/status', { headers: authHeader() });
    if (!response.ok) throw new Error('Could not refresh HEMS status.');
    status.value = await response.json();
    error.value = '';
}
async function poll() {
    try {
        await refresh();
    } catch (e) {
        error.value = String(e);
    }
    if (!disposed) timer = setTimeout(poll, 1000);
}
async function send(name: Target, op: HemsOperation) {
    busy.value = true;
    notice.value = '';
    error.value = '';
    try {
        const command = {
            target: name,
            action: op.action,
            ...(op.has_input ? { value: values.value[`${name}-${op.action}`] ?? op.min } : {}),
        };
        const body = new FormData();
        body.append('data', JSON.stringify(command));
        const response = await fetch('/api/hems/command', { method: 'POST', headers: authHeader(), body });
        const data = await response.json();
        if (!response.ok || !data.accepted) throw new Error(data.message || 'Command rejected');
        notice.value = `${controlText(op.action)} requested (command ${data.command_id}).`;
        await refresh();
    } catch (e) {
        error.value = String(e);
    } finally {
        busy.value = false;
    }
}
onMounted(poll);
onUnmounted(() => {
    disposed = true;
    clearTimeout(timer);
});
</script>
