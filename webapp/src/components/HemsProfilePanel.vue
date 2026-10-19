<template>
    <CardElement :text="$t('profiles.Title')" textVariant="text-bg-primary">
        <h3 class="h5">{{ $t('profiles.Repository') }}</h3>
        <p>{{ $t('profiles.RepositoryHint') }}</p>
        <p class="small text-muted">
            {{ catalogRepository }} · {{ catalogSourceRef
            }}<template v-if="catalog">
                · <code>{{ catalog.source_commit.slice(0, 12) }}</code></template
            >
        </p>
        <p v-if="catalogLoading" role="status">{{ $t('profiles.CatalogLoading') }}</p>
        <div v-if="catalogError" class="alert alert-warning" role="alert">{{ catalogError }}</div>
        <button
            type="button"
            class="btn btn-outline-secondary btn-sm mb-3"
            :disabled="catalogLoading || busy"
            @click="loadCatalog"
        >
            {{ $t('profiles.CatalogReload') }}
        </button>
        <section v-for="group in catalogGroups" :key="group.category" class="mb-3">
            <h4 class="h6">{{ categoryText(group.category) }}</h4>
            <div v-for="entry in group.profiles" :key="entry.id" class="border rounded p-2 mb-2">
                <strong>{{ entry.vendor }} / {{ entry.model }}</strong
                ><br />
                <code>{{ entry.id }}</code
                ><br />
                <span class="small">{{ entry.schema }} · {{ entry.source_revision }}</span
                ><br />
                <span class="small">{{ $t('profiles.CatalogState.' + repositoryState(entry)) }}</span>
                <div class="mt-2">
                    <button
                        type="button"
                        class="btn btn-primary btn-sm"
                        :disabled="busy || repositoryState(entry) === 'current'"
                        @click="importRepository(entry)"
                    >
                        {{ $t(profiles.some((p) => p.id === entry.id) ? 'profiles.UpdateReplace' : 'profiles.Import') }}
                    </button>
                </div>
            </div>
        </section>
        <hr />
        <h3 class="h5">{{ $t('profiles.Installed') }}</h3>
        <p>{{ $t('profiles.Hint') }}</p>
        <p class="text-danger" role="alert" v-if="error">{{ error }}</p>
        <p role="status" v-if="notice">{{ notice }}</p>
        <p v-if="corrupt">{{ $t('profiles.Corrupt', { count: corrupt }) }}</p>
        <div class="table-responsive mt-3">
            <table class="table">
                <thead>
                    <tr>
                        <th>{{ $t('profiles.Identity') }}</th>
                        <th>{{ $t('profiles.Source') }}</th>
                        <th>{{ $t('profiles.Actions') }}</th>
                    </tr>
                </thead>
                <tbody>
                    <tr v-for="p in profiles" :key="p.id">
                        <td>
                            <strong>{{ p.id }}</strong
                            ><br />{{ p.vendor }} / {{ p.model }}<br />
                            {{ $t(p.origin === 'Built-in' ? 'profiles.Builtin' : 'profiles.Imported') }}<br />{{
                                p.schema
                            }}<br />
                            {{ $t('profiles.Entries', { count: p.telemetry_count }) }}<br />
                            {{ controlText(p.control_reason) }}<br />
                            {{ $t('profiles.IdentityMetadata') }}:
                            {{ $t(p.identity_metadata ? 'profiles.Yes' : 'profiles.No') }} ·
                            {{ $t('profiles.RecoveryMetadata') }}:
                            {{ $t(p.recovery_metadata ? 'profiles.Yes' : 'profiles.No') }}<br />
                            {{ $t('profiles.Capabilities') }}: {{ capabilityText(p.capabilities) }}
                            <template v-if="p.used_by.length">
                                <br />{{ $t('profiles.UsedBy') }}: {{ usedByText(p.used_by) }}
                            </template>
                        </td>
                        <td>{{ p.source }} / {{ p.source_revision }}<br />{{ p.author }}<br />{{ p.license }}</td>
                        <td>
                            <button
                                type="button"
                                class="btn btn-secondary btn-sm me-1"
                                :disabled="busy"
                                @click="view(p.id)"
                            >
                                {{ $t('profiles.View') }}
                            </button>
                            <button
                                type="button"
                                class="btn btn-secondary btn-sm me-1"
                                :disabled="busy"
                                @click="download(p.id)"
                            >
                                {{ $t('profiles.Export') }}
                            </button>
                            <button
                                v-if="p.origin === 'Imported'"
                                type="button"
                                class="btn btn-danger btn-sm"
                                :disabled="busy || p.in_use"
                                :title="p.in_use ? $t('profiles.DeleteBlocked') : ''"
                                @click="remove(p.id)"
                            >
                                {{ $t('profiles.Delete') }}
                            </button>
                            <div v-if="p.in_use" class="small text-danger mt-1">
                                {{ $t('profiles.DeleteBlocked') }}
                            </div>
                        </td>
                    </tr>
                </tbody>
            </table>
        </div>
        <details class="my-3">
            <summary>{{ $t('profiles.ManualImport') }}</summary>
            <label for="profile-json" class="form-label">{{ $t('profiles.File') }}</label>
            <input
                id="profile-json"
                class="form-control mb-2"
                type="file"
                accept=".json,application/json"
                @change="selectFile"
            />
            <div class="form-check mb-2">
                <input id="profile-replace" class="form-check-input" type="checkbox" v-model="replace" />
                <label for="profile-replace" class="form-check-label">{{ $t('profiles.Replace') }}</label>
            </div>
            <button class="btn btn-primary me-2" type="button" :disabled="busy || !file" @click="upload(true)">
                {{ $t('profiles.Import') }}
            </button>
            <button class="btn btn-secondary" type="button" :disabled="busy || !file" @click="upload(false)">
                {{ $t('profiles.Validate') }}
            </button>
        </details>
        <template v-if="selected">
            <pre class="border p-2 overflow-auto">{{ JSON.stringify(selected, null, 2) }}</pre>
            <p>{{ $t('profiles.DecodeHint') }}</p>
            <label for="profile-key">{{ $t('profiles.Key') }}</label>
            <select id="profile-key" class="form-select mb-2" v-model="decodeKey">
                <option v-for="entry in selected.telemetry" :key="entry.key" :value="entry.key">{{ entry.key }}</option>
            </select>
            <label for="profile-hex">{{ $t('profiles.Hex') }}</label>
            <input id="profile-hex" class="form-control mb-2" v-model="hex" maxlength="8" />
            <button type="button" class="btn btn-secondary" :disabled="busy" @click="decodeSample">
                {{ $t('profiles.Decode') }}
            </button>
            <pre role="status" v-if="decoded">{{ decoded }}</pre>
        </template>
    </CardElement>
</template>
<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import CardElement from '@/components/CardElement.vue';
import { controlText } from '@/hemsControl';
import { authHeader } from '@/utils/authentication';
import {
    catalogRepository,
    catalogSourceRef,
    CatalogError,
    fetchCatalog,
    fetchCatalogProfile,
    installedCatalogState,
    type CatalogProfile,
    type ProfileCatalog,
} from '@/utils/profileCatalog';

const emit = defineEmits(['profiles-changed']);
const { t } = useI18n();
type Summary = {
    id: string;
    vendor: string;
    model: string;
    origin: string;
    schema: string;
    source: string;
    source_revision: string;
    author: string;
    license: string;
    telemetry_count: number;
    capabilities: string[];
    used_by: string[];
    in_use: boolean;
    control_supported: boolean;
    control_reason: string;
    identity_metadata: boolean;
    recovery_metadata: boolean;
    content_sha256?: string;
};
type Profile = { id: string; telemetry: { key: string }[] };
const profiles = ref<Summary[]>([]),
    corrupt = ref(0),
    error = ref(''),
    notice = ref(''),
    busy = ref(false);
const file = ref<File>(),
    replace = ref(true),
    selected = ref<Profile>(),
    decodeKey = ref(''),
    hex = ref('0000'),
    decoded = ref('');
const catalog = ref<ProfileCatalog>(),
    catalogLoading = ref(false),
    catalogError = ref('');
const catalogGroups = computed(() => {
    const groups = new Map<string, CatalogProfile[]>();
    for (const entry of catalog.value?.profiles ?? []) {
        const entries = groups.get(entry.category) ?? [];
        entries.push(entry);
        groups.set(entry.category, entries);
    }
    return [...groups].sort(([a], [b]) => a.localeCompare(b)).map(([category, profiles]) => ({ category, profiles }));
});
function categoryText(category: string) {
    return category === 'storage'
        ? t('profiles.StorageCapability')
        : category === 'evse'
          ? t('profiles.EvseCategory')
          : category;
}
function repositoryState(entry: CatalogProfile) {
    return installedCatalogState(
        entry,
        profiles.value.find((p) => p.id === entry.id)
    );
}
async function loadCatalog() {
    catalogLoading.value = true;
    catalogError.value = '';
    try {
        catalog.value = await fetchCatalog();
    } catch (e) {
        catalogError.value = t('profiles.CatalogError.' + (e instanceof CatalogError ? e.code : 'Network'));
    } finally {
        catalogLoading.value = false;
    }
}
async function importRepository(entry: CatalogProfile) {
    if (!catalog.value) return;
    const installed = profiles.value.some((p) => p.id === entry.id);
    if (installed && !window.confirm(t('profiles.ConfirmReplace', { id: entry.id }))) return;
    await run(async () => {
        try {
            const text = await fetchCatalogProfile(catalog.value!, entry);
            await submitProfile(text, true, installed ? entry.id : undefined);
        } catch (e) {
            if (e instanceof CatalogError) throw new Error(t('profiles.CatalogError.' + e.code));
            throw e;
        }
    });
}
async function api(path: string, options: RequestInit = {}) {
    const response = await fetch('/api/hems/profiles' + path, { ...options, headers: options.headers ?? authHeader() });
    const data = await response.json();
    if (!response.ok) throw new Error(data.message || `${response.status} ${response.statusText}`);
    return data;
}
async function run(action: () => Promise<void>) {
    busy.value = true;
    error.value = '';
    notice.value = '';
    try {
        await action();
    } catch (e) {
        error.value = e instanceof Error ? e.message : String(e);
    } finally {
        busy.value = false;
    }
}
async function refresh() {
    const data = await api('');
    profiles.value = data.profiles;
    corrupt.value = data.corrupt_slots;
}
function capabilityText(values: string[]) {
    if (!values.length) return t('profiles.NoCapabilities');
    return values
        .map((value) => (value === 'storage' ? t('profiles.StorageCapability') : value === 'evse' ? 'EVSE' : value))
        .join(', ');
}
function usedByText(values: string[]) {
    return values
        .map((value) => (value === 'storage' ? t('profiles.StorageConsumer') : value === 'evse' ? 'EVSE' : value))
        .join(', ');
}
async function selectFile(event: Event) {
    file.value = (event.target as HTMLInputElement).files?.[0];
    notice.value = '';
    if (!file.value || file.value.size > 6144) return;
    try {
        const candidate = JSON.parse(await file.value.text());
        notice.value = t('profiles.Preview', { id: candidate.id, schema: candidate.schema });
    } catch {
        error.value = t('profiles.InvalidJson');
    }
}
async function upload(persist: boolean) {
    await run(async () => {
        if (!file.value || file.value.size > 6144 || !file.value.name.toLowerCase().endsWith('.json'))
            throw new Error(t('profiles.FileLimit'));
        const text = await file.value.text();
        const id = JSON.parse(text).id;
        await submitProfile(
            text,
            persist,
            persist && replace.value && profiles.value.some((p) => p.id === id) ? id : undefined
        );
    });
}
async function submitProfile(text: string, persist: boolean, replaceId?: string) {
    const query = replaceId ? '?replace=' + encodeURIComponent(replaceId) : '';
    const headers = authHeader();
    headers.set('Content-Type', 'application/json');
    await api((persist ? '/import' : '/validate') + query, { method: 'POST', headers, body: text });
    notice.value = t(persist ? 'profiles.ImportedNotice' : 'profiles.ValidNotice');
    selected.value = undefined;
    decoded.value = '';
    await refresh();
    if (persist) emit('profiles-changed');
}

async function view(id: string) {
    await run(async () => {
        selected.value = await api('/export?id=' + encodeURIComponent(id));
        decodeKey.value = selected.value?.telemetry[0]?.key ?? '';
        decoded.value = '';
    });
}
async function download(id: string) {
    await run(async () => {
        const response = await fetch('/api/hems/profiles/export?id=' + encodeURIComponent(id), {
            headers: authHeader(),
        });
        if (!response.ok) {
            const data = await response.json();
            throw new Error(data.message || response.statusText);
        }
        const url = URL.createObjectURL(await response.blob());
        const anchor = document.createElement('a');
        anchor.href = url;
        anchor.download = id + '.json';
        anchor.click();
        URL.revokeObjectURL(url);
    });
}
async function remove(id: string) {
    if (!window.confirm(t('profiles.ConfirmDelete', { id }))) return;
    await run(async () => {
        await api('/delete?id=' + encodeURIComponent(id), { method: 'POST' });
        selected.value = undefined;
        decoded.value = '';
        await refresh();
        emit('profiles-changed');
    });
}
async function decodeSample() {
    await run(async () => {
        decoded.value = JSON.stringify(
            await api(
                '/decode?' + new URLSearchParams({ id: selected.value?.id ?? '', key: decodeKey.value, hex: hex.value })
            )
        );
    });
}
onMounted(() => {
    void run(refresh);
    void loadCatalog();
});
</script>
