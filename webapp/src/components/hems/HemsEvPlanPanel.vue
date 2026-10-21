<template>
    <CardElement :text="copy.title" textVariant="text-bg-primary" add-space>
        <p class="small text-body-secondary">{{ copy.shadow }}</p>
        <div v-if="error" class="alert alert-warning" role="alert">{{ error }}</div>
        <div v-if="notice" class="alert alert-success" role="status">{{ notice }}</div>
        <template v-if="plan && !error">
            <p class="fw-semibold">{{ reason(plan.reason) }}</p>
            <template v-if="view !== 'settings'">
                <dl class="row mb-2">
                    <dt class="col-sm-6">{{ copy.action }}</dt>
                    <dd class="col-sm-6">
                        {{ plan.current_action === 'charge' ? copy.charge + ' · ' + plan.current_a + ' A' : copy.wait }}
                    </dd>
                    <dt class="col-sm-6">{{ copy.next }}</dt>
                    <dd class="col-sm-6">
                        {{ date(plan.next_change) }} · {{ plan.next_action === 'charge' ? copy.charge : copy.wait }}
                    </dd>
                    <dt class="col-sm-6">{{ copy.planned }}</dt>
                    <dd class="col-sm-6">{{ kwh(plan.required_wh) }} / {{ kwh(plan.planned_wh) }}</dd>
                    <dt class="col-sm-6">{{ copy.capacity }}</dt>
                    <dd class="col-sm-6">{{ kwh(plan.deliverable_wh) }}</dd>
                    <dt class="col-sm-6">{{ copy.completion }}</dt>
                    <dd class="col-sm-6">{{ date(plan.completion) }}</dd>
                    <dt class="col-sm-6">{{ copy.shortfall }}</dt>
                    <dd class="col-sm-6">{{ kwh(plan.shortfall_wh) }}</dd>
                    <dt class="col-sm-6">{{ copy.cost }}</dt>
                    <dd class="col-sm-6">
                        {{
                            plan.cost_nano == null
                                ? '—'
                                : (plan.cost_nano / 1e9).toLocaleString(locale, { maximumFractionDigits: 4 }) +
                                  ' ' +
                                  plan.currency
                        }}
                    </dd>
                </dl>
                <details v-if="windows.length">
                    <summary>{{ copy.windows }} ({{ windows.length }})</summary>
                    <ul class="mt-2">
                        <li v-for="window in windows" :key="window.start">
                            {{ date(window.start) }} → {{ date(window.end) }} · {{ window.current }} A
                        </li>
                    </ul>
                </details>
                <p v-if="plan.coverage_reason" class="small text-warning-emphasis mt-2">
                    {{ reason(plan.coverage_reason) }}
                </p>
                <p v-if="plan.enabled && !plan.pv_included" class="small mt-2">{{ copy.tariffOnly }}</p>
                <p v-if="plan.enabled" class="small mt-2">{{ reason(plan.storage_interaction) }}</p>
                <p class="small text-body-secondary">{{ copy.staged }}</p>
            </template>
            <form v-if="view === 'settings' && draft" @submit.prevent="save">
                <label class="form-check-label mb-3"
                    ><input v-model="draft.enabled" type="checkbox" class="form-check-input me-2" />{{
                        copy.enabled
                    }}</label
                >
                <div class="row g-3">
                    <div class="col-md-6">
                        <label for="ev-deadline" class="form-label">{{ copy.deadline }} · {{ zone }}</label
                        ><input
                            id="ev-deadline"
                            :value="deadlineText"
                            type="datetime-local"
                            class="form-control"
                            :required="draft.enabled"
                            @input="setDeadline"
                        />
                    </div>
                    <div class="col-md-6">
                        <label for="ev-energy" class="form-label">{{ copy.energy }} (kWh)</label
                        ><input
                            id="ev-energy"
                            :value="draft.required_wh / 1000"
                            type="number"
                            min="0"
                            max="1000"
                            step="0.001"
                            required
                            class="form-control"
                            @input="
                                draft.required_wh = Math.round(Number(($event.target as HTMLInputElement).value) * 1000)
                            "
                        />
                    </div>
                    <div v-if="candidates.length > 1" class="col-12">
                        <label for="ev-offset" class="form-label">{{ copy.ambiguous }}</label
                        ><select id="ev-offset" v-model.number="draft.deadline" class="form-select" required>
                            <option :value="0" disabled>—</option>
                            <option v-for="epoch in candidates" :key="epoch" :value="epoch">
                                {{ date(epoch) }} · {{ new Date(epoch * 1000).toISOString() }}
                            </option>
                        </select>
                    </div>
                    <div v-for="field in currentFields" :key="field.key" class="col-md-6">
                        <label :for="'ev-' + field.key" class="form-label">{{ field.label }} (A)</label
                        ><input
                            :id="'ev-' + field.key"
                            v-model.number="draft[field.key]"
                            type="number"
                            min="6"
                            max="80"
                            step="1"
                            required
                            class="form-control"
                        />
                    </div>
                </div>
                <label class="form-check-label d-block my-3"
                    ><input v-model="draft.pv_preferred" type="checkbox" class="form-check-input me-2" />{{
                        copy.pv
                    }}</label
                >
                <label class="form-check-label d-block my-3"
                    ><input v-model="draft.max_price_enabled" type="checkbox" class="form-check-input me-2" />{{
                        copy.cap
                    }}</label
                >
                <label v-if="draft.max_price_enabled" for="ev-price" class="form-label">{{ copy.price }} (ct/kWh)</label
                ><input
                    v-if="draft.max_price_enabled"
                    id="ev-price"
                    :value="draft.max_price_micros_per_kwh / 10000"
                    type="number"
                    min="-214748.3648"
                    max="214748.3647"
                    step="0.0001"
                    required
                    class="form-control mb-3"
                    @input="
                        draft.max_price_micros_per_kwh = Math.round(
                            Number(($event.target as HTMLInputElement).value) * 10000
                        )
                    "
                />
                <details class="border rounded p-3 mb-3">
                    <summary>{{ copy.advanced }}</summary>
                    <p class="form-text">{{ copy.electrical }}</p>
                    <label for="ev-voltage" class="form-label">{{ copy.voltage }} (V)</label
                    ><input
                        id="ev-voltage"
                        v-model.number="draft.nominal_voltage"
                        type="number"
                        min="0"
                        max="250"
                        step="1"
                        required
                        class="form-control mb-3"
                    /><label for="ev-phases" class="form-label">{{ copy.phases }}</label
                    ><select id="ev-phases" v-model.number="draft.phases" class="form-select">
                        <option :value="0">{{ copy.unconfigured }}</option>
                        <option v-for="n in 3" :key="n" :value="n">{{ n }}</option>
                    </select>
                </details>
                <p class="form-text">{{ copy.model }}</p>
                <button class="btn btn-primary" :disabled="busy">{{ copy.save }}</button>
            </form>
            <details v-if="view === 'diagnostics'">
                <summary>{{ copy.advanced }}</summary>
                <pre class="small overflow-auto">{{ JSON.stringify(plan, null, 2) }}</pre>
            </details>
        </template>
    </CardElement>
</template>
<script setup lang="ts">
import { computed, getCurrentInstance, onMounted, onUnmounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { localDeadline } from '@/utils/hemsPolicy';
import { evDeadlineCandidates } from '@/utils/hemsEv';
import { evMessages } from '@/hemsEvMessages';
import type { EvShadowConfig, EvShadowPlan } from '@/types/HemsEvPlan';
import type { EnergyView } from '@/types/HemsEnergy';
const props = withDefaults(defineProps<{ view?: EnergyView }>(), { view: 'overview' });
const { locale } = useI18n();
const copy = computed(() => evMessages[locale.value.split('-')[0] as keyof typeof evMessages] || evMessages.en);
const plan = ref<EvShadowPlan>(),
    draft = ref<EvShadowConfig>();
const error = ref(''),
    notice = ref(''),
    busy = ref(false),
    deadlineText = ref('');
const zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
const candidates = computed(() => evDeadlineCandidates(deadlineText.value));
const currentFields = computed(
    () =>
        [
            { key: 'min_current_a', label: copy.value.min },
            { key: 'max_current_a', label: copy.value.max },
        ] as const
);
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter,
    router = useRouter();
const reason = (code: string) =>
    copy.value.reasons[code as keyof typeof evMessages.en.reasons] || copy.value.reasons.waiting_for_calculation;
const date = (epoch: number | null) =>
    epoch
        ? new Date(epoch * 1000).toLocaleString(locale.value, {
              month: 'short',
              day: 'numeric',
              hour: '2-digit',
              minute: '2-digit',
              timeZoneName: 'shortOffset',
          })
        : '—';
const kwh = (wh: number | null) =>
    wh == null ? '—' : (wh / 1000).toLocaleString(locale.value, { maximumFractionDigits: 3 }) + ' kWh';
const windows = computed(() => {
    const blocks: { start: number; end: number; current: number }[] = [];
    for (const step of plan.value?.schedule || []) {
        if (!step.current_a) continue;
        const last = blocks[blocks.length - 1];
        if (last?.end === step.start && last.current === step.current_a) last.end = step.charge_end;
        else blocks.push({ start: step.start, end: step.charge_end, current: step.current_a });
    }
    return blocks;
});
function setDeadline(event: Event) {
    const input = event.target as HTMLInputElement;
    deadlineText.value = input.value;
    if (draft.value) draft.value.deadline = candidates.value.length === 1 ? (candidates.value[0] ?? 0) : 0;
    input.setCustomValidity(candidates.value.length || !draft.value?.enabled ? '' : copy.value.invalid);
}
async function request(path: string, body?: FormData) {
    return fetch(path, { headers: authHeader(), ...(body ? { method: 'POST', body } : {}) }).then((r) =>
        handleResponse(r, emitter, router, true)
    );
}
let disposed = false,
    loading = false;
async function load(reset = false) {
    if (loading) return;
    loading = true;
    try {
        const first = await request('/api/hems/optimizer?all=1&offset=0');
        const ev = first.ev_shadow as EvShadowPlan | undefined;
        if (!ev) {
            plan.value = undefined;
            return;
        }
        let page = ev;
        if (plan.value?.plan_revision === ev.plan_revision && !ev.stale_inputs) {
            ev.schedule = plan.value.schedule;
            page = { ...ev, has_more: false };
        }
        for (let offset = 24; props.view !== 'settings' && page.has_more && offset < 256; offset += 24) {
            page = (await request(`/api/hems/optimizer?all=1&offset=${offset}`)).ev_shadow;
            if (page.plan_revision !== ev.plan_revision || page.stale_inputs)
                throw new Error(copy.value.reasons.inputs_changed_recalculation_required);
            ev.schedule.push(...page.schedule);
        }
        if (disposed) return;
        plan.value = ev;
        if (!draft.value || reset) {
            draft.value = structuredClone(ev.config);
            deadlineText.value = localDeadline(draft.value.deadline);
        }
        error.value = '';
    } catch (e) {
        if (!disposed) error.value = String(e);
    } finally {
        loading = false;
    }
}
async function save() {
    if (!draft.value || (draft.value.enabled && !draft.value.deadline)) {
        error.value = copy.value.invalid;
        return;
    }
    busy.value = true;
    try {
        const body = new FormData();
        body.append('data', JSON.stringify({ ev_shadow: draft.value }));
        await request('/api/hems/optimizer/config', body);
        notice.value = copy.value.saved;
        await load(true);
    } catch (e) {
        error.value = String(e);
    } finally {
        busy.value = false;
    }
}
let timer: ReturnType<typeof setInterval> | undefined;
onMounted(() => {
    void load();
    timer = setInterval(() => {
        if (!busy.value) void load();
    }, 15000);
});
onUnmounted(() => {
    disposed = true;
    if (timer) clearInterval(timer);
});
</script>
