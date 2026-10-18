export interface FirmwareBuild {
    file: string;
    size: number;
    md5: string;
    sha256: string;
}
export interface FirmwareManifest {
    schema: string;
    repository: string;
    channel: string;
    branch: string;
    commit: string; // Exact image/build identity, including synthetic PR merge commits.
    source_commit?: string;
    tested_merge_commit?: string;
    published_at: string;
    builds: Record<string, FirmwareBuild>;
}
export interface InstalledProvenance {
    git_sha?: string;
    source_commit?: string;
    tested_merge_commit?: string;
    git_branch: string;
    pioenv: string;
    update_repository?: string;
    update_channel?: string;
}
export function isCommit(value: unknown): value is string {
    return typeof value === 'string' && /^[0-9a-f]{40}$/i.test(value);
}
export function sameCommit(a: unknown, b: unknown): boolean {
    return isCommit(a) && isCommit(b) && a.toLowerCase() === b.toLowerCase();
}
export function validManifest(value: unknown, repository: string, channel: string): value is FirmwareManifest {
    if (!value || typeof value !== 'object') return false;
    const manifest = value as Partial<FirmwareManifest>;
    return (
        manifest.schema === 'opendtu-hems-update/v1' &&
        manifest.repository === repository &&
        manifest.channel === channel &&
        typeof manifest.branch === 'string' &&
        isCommit(manifest.commit) &&
        (manifest.source_commit === undefined || isCommit(manifest.source_commit)) &&
        (manifest.tested_merge_commit === undefined || isCommit(manifest.tested_merge_commit)) &&
        typeof manifest.published_at === 'string' &&
        !!manifest.builds &&
        typeof manifest.builds === 'object'
    );
}
export function exactImageInstalled(installed: InstalledProvenance, manifest: FirmwareManifest): boolean {
    return (
        sameCommit(installed.git_sha, manifest.commit) &&
        installed.update_repository === manifest.repository &&
        installed.update_channel === manifest.channel &&
        installed.git_branch === manifest.branch &&
        !!manifest.builds[installed.pioenv]
    );
}
// Old firmware can recover source provenance only from a matching published image in its own channel.
// Never adopt the source of a newer image, a different channel, or a shortened SHA match.
export function installedSourceCommit(installed: InstalledProvenance, manifest?: FirmwareManifest): string | undefined {
    if (isCommit(installed.source_commit)) return installed.source_commit;
    if (manifest && exactImageInstalled(installed, manifest) && isCommit(manifest.source_commit))
        return manifest.source_commit;
    return undefined;
}
export function sourceComparisonBase(installed: InstalledProvenance, manifest?: FirmwareManifest): string | undefined {
    return installedSourceCommit(installed, manifest) ?? (isCommit(installed.git_sha) ? installed.git_sha : undefined);
}
export function sourceHasAdvanced(comparison: { ahead_by: number }): boolean {
    return Number.isInteger(comparison.ahead_by) && comparison.ahead_by > 0;
}
