import { sha256 } from '@noble/hashes/sha2.js';
import { bytesToHex } from '@noble/hashes/utils.js';

// A generated static index; neither the catalog nor a profile is fetched by ESP32.
export const catalogRepository = 'dawid-woitaschek/OpenDTU-HEMS';
export const catalogSourceRef = 'integration/hems-next';
export const catalogUrl = `https://raw.githubusercontent.com/${catalogRepository}/device-profile-catalog/catalog.json`;
const rawRoot = `https://raw.githubusercontent.com/${catalogRepository}/`;
export const maxProfileBytes = 6144;
const maxCatalogBytes = 256 * 1024;
export interface CatalogProfile {
    id: string;
    category: string;
    vendor: string;
    model: string;
    schema: string;
    source_revision: string;
    path: string;
    size: number;
    sha256: string;
    installed_sha256: string;
}
export interface ProfileCatalog {
    version: 1;
    source_commit: string;
    profiles: CatalogProfile[];
}
export class CatalogError extends Error {
    constructor(public readonly code: string) {
        super(code);
    }
}
const digest = (value: unknown): value is string => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const label = (value: unknown, max: number): value is string =>
    typeof value === 'string' && value.length > 0 && value.length <= max;
export function parseCatalog(value: unknown): ProfileCatalog {
    if (!value || typeof value !== 'object') throw new CatalogError('Malformed');
    const catalog = value as ProfileCatalog;
    if (
        catalog.version !== 1 ||
        typeof catalog.source_commit !== 'string' ||
        !/^[a-f0-9]{40}$/.test(catalog.source_commit) ||
        !Array.isArray(catalog.profiles) ||
        catalog.profiles.length > 256
    )
        throw new CatalogError('Malformed');
    const ids = new Set<string>(),
        paths = new Set<string>();
    for (const p of catalog.profiles) {
        if (
            !p ||
            !label(p.id, 63) ||
            !/^[A-Za-z0-9._-]+$/.test(p.id) ||
            !label(p.category, 32) ||
            !/^[a-z][a-z0-9_-]*$/.test(p.category) ||
            !label(p.vendor, 96) ||
            !label(p.model, 128) ||
            !label(p.source_revision, 192) ||
            !/^opendtu-hems-modbus-profile\/v[12]$/.test(p.schema) ||
            typeof p.path !== 'string' ||
            !/^[a-z][a-z0-9_-]*\/[A-Za-z0-9][A-Za-z0-9._-]*\.json$/.test(p.path) ||
            p.path.split('/')[0] !== p.category ||
            !Number.isInteger(p.size) ||
            p.size < 1 ||
            p.size > maxProfileBytes ||
            !digest(p.sha256) ||
            !digest(p.installed_sha256) ||
            ids.has(p.id) ||
            paths.has(p.path)
        )
            throw new CatalogError('Malformed');
        ids.add(p.id);
        paths.add(p.path);
    }
    return catalog;
}
async function readBounded(response: Response, max: number): Promise<Uint8Array> {
    if (!response.body) throw new CatalogError('Malformed');
    const reader = response.body.getReader();
    const chunks: Uint8Array[] = [];
    let size = 0;
    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            size += value.byteLength;
            if (size > max) throw new CatalogError('TooLarge');
            chunks.push(value);
        }
    } catch (error) {
        await reader.cancel();
        throw error;
    } finally {
        reader.releaseLock();
    }
    const bytes = new Uint8Array(size);
    let offset = 0;
    for (const chunk of chunks) {
        bytes.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return bytes;
}
async function get(url: string, max: number, selected = false): Promise<Uint8Array> {
    let response: Response;
    try {
        response = await fetch(url, {
            credentials: 'omit',
            referrerPolicy: 'no-referrer',
            redirect: 'error',
            signal: AbortSignal.timeout(20000),
        });
    } catch {
        throw new CatalogError('Network');
    }
    if (!response.ok)
        throw new CatalogError(response.status === 404 ? (selected ? 'ProfileNotFound' : 'NotFound') : 'Unavailable');
    try {
        return await readBounded(response, max);
    } catch (e) {
        if (e instanceof CatalogError) throw e;
        throw new CatalogError('Network');
    }
}
function json(bytes: Uint8Array): unknown {
    try {
        return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
    } catch {
        throw new CatalogError('Malformed');
    }
}
export async function fetchCatalog(): Promise<ProfileCatalog> {
    return parseCatalog(json(await get(catalogUrl, maxCatalogBytes)));
}
export function installedCatalogState(
    profile: CatalogProfile,
    installed?: { content_sha256?: string }
): 'missing' | 'current' | 'different' | 'unknown' {
    if (!installed) return 'missing';
    if (!installed.content_sha256) return 'unknown';
    return installed.content_sha256 === profile.installed_sha256 ? 'current' : 'different';
}
export async function fetchCatalogProfile(catalog: ProfileCatalog, entry: CatalogProfile): Promise<string> {
    // Revalidate before constructing a URL. Pin to the catalog commit to prevent ref races.
    parseCatalog(catalog);
    if (!catalog.profiles.includes(entry)) throw new CatalogError('Malformed');
    const bytes = await get(`${rawRoot}${catalog.source_commit}/device-profiles/${entry.path}`, maxProfileBytes, true);
    if (bytes.byteLength !== entry.size || bytesToHex(sha256(bytes)) !== entry.sha256)
        throw new CatalogError('Integrity');
    let profile: { id?: string; schema?: string } | null;
    try {
        profile = json(bytes) as typeof profile;
    } catch {
        throw new CatalogError('MalformedProfile');
    }
    if (!profile || profile.id !== entry.id || profile.schema !== entry.schema)
        throw new CatalogError('MalformedProfile');
    return new TextDecoder().decode(bytes);
}
