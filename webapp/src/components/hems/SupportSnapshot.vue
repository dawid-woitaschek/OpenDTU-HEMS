<template>
    <CardElement :text="copy.title" textVariant="text-bg-primary" add-space>
        <p class="mb-2">{{ copy.description }}</p>
        <div class="alert alert-info py-2" role="note">
            {{ copy.privacy }}
        </div>

        <div class="d-flex flex-column gap-2 mb-3">
            <label class="form-check-label">
                <input v-model="includeNetwork" class="form-check-input me-2" type="checkbox" />
                {{ copy.includeNetwork }}
            </label>
            <label class="form-check-label">
                <input v-model="includeLocation" class="form-check-input me-2" type="checkbox" />
                {{ copy.includeLocation }}
            </label>
        </div>

        <div class="d-flex flex-wrap gap-2">
            <button type="button" class="btn btn-primary" :disabled="busy" @click="download">
                <span v-if="busy" class="spinner-border spinner-border-sm me-2" role="status"></span>
                {{ busy ? copy.collecting : copy.download }}
            </button>
            <button type="button" class="btn btn-outline-secondary" :disabled="busy" @click="copySnapshot">
                {{ copy.clipboard }}
            </button>
        </div>

        <p v-if="notice" class="alert alert-success py-2 mt-3 mb-0" role="status">{{ notice }}</p>
        <p v-if="error" class="alert alert-warning py-2 mt-3 mb-0" role="alert">{{ error }}</p>
        <p v-if="lastBytes" class="small text-body-secondary mt-2 mb-0">
            {{ copy.lastSize }}: {{ formatBytes(lastBytes) }}
        </p>
    </CardElement>
</template>

<script setup lang="ts">
import { computed, getCurrentInstance, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';

type JsonObject = Record<string, unknown>;
type Collected = { ok: true; data: unknown } | { ok: false; error: string };

type SnapshotCopy = {
    title: string;
    description: string;
    privacy: string;
    includeNetwork: string;
    includeLocation: string;
    download: string;
    clipboard: string;
    collecting: string;
    downloaded: string;
    copied: string;
    copyFailed: string;
    lastSize: string;
};

const texts: Record<string, SnapshotCopy> = {
    de: {
        title: 'Support & Diagnose',
        description:
            'Erstellt auf Abruf einen JSON-Snapshot aus vorhandenen Diagnose-APIs. Die Datei wird nur im Browser erzeugt und nicht auf der OpenDTU gespeichert.',
        privacy:
            'Passwörter, Tokens, Schlüssel, Cookies und Zugangsdaten werden immer entfernt. Netzwerk- und Standortdaten sind standardmäßig ausgeblendet.',
        includeNetwork: 'Netzwerkadressen, Hostnamen und URLs einschließen',
        includeLocation: 'PV-Standort / Koordinaten einschließen',
        download: 'Diagnose-Snapshot herunterladen',
        clipboard: 'In Zwischenablage kopieren',
        collecting: 'Diagnosedaten werden gesammelt …',
        downloaded: 'Diagnose-Snapshot erstellt.',
        copied: 'Diagnose-Snapshot in die Zwischenablage kopiert.',
        copyFailed: 'Kopieren ist in diesem Browser-Kontext nicht verfügbar. Der Download funktioniert weiterhin.',
        lastSize: 'Letzte Snapshot-Größe',
    },
    en: {
        title: 'Support & diagnostics',
        description:
            'Creates an on-demand JSON snapshot from existing diagnostic APIs. The file is assembled only in the browser and is not stored on the OpenDTU.',
        privacy:
            'Passwords, tokens, keys, cookies and credentials are always removed. Network and location data are hidden by default.',
        includeNetwork: 'Include network addresses, hostnames and URLs',
        includeLocation: 'Include PV location / coordinates',
        download: 'Download diagnostic snapshot',
        clipboard: 'Copy to clipboard',
        collecting: 'Collecting diagnostics …',
        downloaded: 'Diagnostic snapshot created.',
        copied: 'Diagnostic snapshot copied to clipboard.',
        copyFailed: 'Clipboard access is unavailable in this browser context. Download remains available.',
        lastSize: 'Last snapshot size',
    },
    fr: {
        title: 'Support et diagnostic',
        description:
            "Crée à la demande un instantané JSON à partir des API de diagnostic existantes. Le fichier est assemblé uniquement dans le navigateur et n'est pas stocké sur l'OpenDTU.",
        privacy:
            'Les mots de passe, jetons, clés, cookies et identifiants sont toujours supprimés. Les données réseau et de localisation sont masquées par défaut.',
        includeNetwork: 'Inclure les adresses réseau, noms d’hôte et URL',
        includeLocation: 'Inclure la localisation PV / les coordonnées',
        download: 'Télécharger le diagnostic',
        clipboard: 'Copier dans le presse-papiers',
        collecting: 'Collecte des diagnostics …',
        downloaded: 'Instantané de diagnostic créé.',
        copied: 'Instantané de diagnostic copié dans le presse-papiers.',
        copyFailed:
            'L’accès au presse-papiers n’est pas disponible dans ce contexte. Le téléchargement reste disponible.',
        lastSize: 'Taille du dernier instantané',
    },
};

const { locale } = useI18n();
const copy = computed<SnapshotCopy>(() => {
    const language = locale.value.split('-')[0] || 'en';
    return texts[language] ?? texts.en!;
});
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
const includeNetwork = ref(false);
const includeLocation = ref(false);
const busy = ref(false);
const notice = ref('');
const error = ref('');
const lastBytes = ref(0);

const SECRET_KEY =
    /(password|passwd|passphrase|secret|token|api[_-]?key|apikey|authorization|cookie|credential|private[_-]?key|psk)/i;
const NETWORK_KEY = /(^|_)(host(name)?|ip(v4|v6)?|gateway|dns|ssid|bssid|mac|address|url|endpoint)(_|$)/i;
const LOCATION_KEY = /(^|_)(lat|latitude|lon|lng|longitude|location|coordinates)(_|$)/i;
const IPV4 = /^(?:\d{1,3}\.){3}\d{1,3}$/;
const MAC = /^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$/i;
const URL_VALUE = /^[a-z][a-z0-9+.-]*:\/\//i;

function sanitize(value: unknown, key = ''): unknown {
    if (value && typeof value === 'object') {
        const object = value as JsonObject;
        const namedSecret = ['name', 'key', 'header'].some(
            (field) => typeof object[field] === 'string' && SECRET_KEY.test(String(object[field]))
        );
        if (Array.isArray(value)) return value.map((entry) => sanitize(entry));
        return Object.fromEntries(
            Object.entries(object).map(([childKey, childValue]) => [
                childKey,
                namedSecret && childKey === 'value' ? '[redacted-secret]' : sanitize(childValue, childKey),
            ])
        );
    }

    if (SECRET_KEY.test(key)) return '[redacted-secret]';
    if (!includeLocation.value && LOCATION_KEY.test(key)) return '[redacted-location]';
    if (!includeNetwork.value && NETWORK_KEY.test(key)) return '[redacted-network]';
    if (
        !includeNetwork.value &&
        typeof value === 'string' &&
        (IPV4.test(value) || MAC.test(value) || URL_VALUE.test(value))
    ) {
        return '[redacted-network]';
    }
    return value;
}

function message(value: unknown): string {
    if (value && typeof value === 'object' && 'message' in value)
        return String((value as { message: unknown }).message);
    return String(value || 'request failed');
}

async function request(path: string): Promise<unknown> {
    return fetch(path, { headers: authHeader() }).then((response) => handleResponse(response, emitter, router, true));
}

async function collect(path: string): Promise<Collected> {
    try {
        return { ok: true, data: await request(path) };
    } catch (reason) {
        return { ok: false, error: message(reason) };
    }
}

async function collectOptimizer(): Promise<Collected> {
    try {
        let offset = 0;
        let page = (await request('/api/hems/optimizer?all=1&offset=0')) as JsonObject;
        const schedule = Array.isArray(page.schedule) ? [...page.schedule] : [];
        let pages = 1;
        while (page.has_more === true && pages < 16) {
            const advance = Array.isArray(page.schedule) && page.schedule.length ? page.schedule.length : 24;
            offset += advance;
            page = (await request(`/api/hems/optimizer?all=1&offset=${offset}`)) as JsonObject;
            if (Array.isArray(page.schedule)) schedule.push(...page.schedule);
            pages += 1;
        }
        return {
            ok: true,
            data: {
                ...page,
                schedule,
                schedule_offset: 0,
                has_more: page.has_more === true && pages >= 16,
                snapshot_pages: pages,
                snapshot_truncated: page.has_more === true && pages >= 16,
            },
        };
    } catch (reason) {
        return { ok: false, error: message(reason) };
    }
}

async function buildSnapshot(): Promise<string> {
    busy.value = true;
    notice.value = '';
    error.value = '';
    try {
        // Deliberately sequential: do not make the ESP32 serialize several large diagnostic responses at once.
        const firmware = await collect('/api/firmware/status');
        // Preserve bounded heap checkpoints and allocation failures in the on-demand export.
        const system = await collect('/api/system/status?heap_history=1');
        const hems = await collect('/api/hems/status');
        const tariff = await collect('/api/hems/energy/tariff?all=1');
        const forecast = await collect('/api/hems/energy/forecast?all=1');
        const optimizer = await collectOptimizer();

        const sources = { firmware, system, hems, tariff, forecast, optimizer };
        const collectionErrors = Object.fromEntries(
            Object.entries(sources)
                .filter(([, result]) => !result.ok)
                .map(([name, result]) => [name, (result as { ok: false; error: string }).error])
        );
        const payload = sanitize({
            schema: 'opendtu-hems-diagnostics/v1',
            generated_at: new Date().toISOString(),
            client: {
                timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
                locale: locale.value,
            },
            redaction: {
                secrets_removed: true,
                network_included: includeNetwork.value,
                location_included: includeLocation.value,
            },
            firmware: firmware.ok ? firmware.data : null,
            system: system.ok ? system.data : null,
            hems: hems.ok ? hems.data : null,
            energy: {
                tariff: tariff.ok ? tariff.data : null,
                forecast: forecast.ok ? forecast.data : null,
            },
            optimizer: optimizer.ok ? optimizer.data : null,
            collection_errors: collectionErrors,
        });
        const json = JSON.stringify(payload, null, 2) + '\n';
        lastBytes.value = new Blob([json]).size;
        return json;
    } finally {
        busy.value = false;
    }
}

function filename() {
    return `opendtu-hems-diagnostics-${new Date().toISOString().replace(/[:.]/g, '-').replace('Z', '')}.json`;
}

async function download() {
    try {
        const json = await buildSnapshot();
        const url = URL.createObjectURL(new Blob([json], { type: 'application/json;charset=utf-8' }));
        const anchor = document.createElement('a');
        anchor.href = url;
        anchor.download = filename();
        document.body.appendChild(anchor);
        anchor.click();
        anchor.remove();
        window.setTimeout(() => URL.revokeObjectURL(url), 0);
        notice.value = copy.value.downloaded;
    } catch (reason) {
        error.value = message(reason);
    }
}

async function legacyCopy(text: string): Promise<boolean> {
    const textarea = document.createElement('textarea');
    textarea.value = text;
    textarea.setAttribute('readonly', '');
    textarea.style.position = 'fixed';
    textarea.style.opacity = '0';
    document.body.appendChild(textarea);
    textarea.select();
    const copied = document.execCommand('copy');
    textarea.remove();
    return copied;
}

async function copySnapshot() {
    try {
        const json = await buildSnapshot();
        let copied = false;
        if (navigator.clipboard && window.isSecureContext) {
            await navigator.clipboard.writeText(json);
            copied = true;
        } else {
            copied = await legacyCopy(json);
        }
        if (!copied) throw new Error(copy.value.copyFailed);
        notice.value = copy.value.copied;
    } catch (reason) {
        error.value = message(reason) || copy.value.copyFailed;
    }
}

function formatBytes(bytes: number) {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
    return `${(bytes / 1024 / 1024).toFixed(2)} MiB`;
}
</script>
