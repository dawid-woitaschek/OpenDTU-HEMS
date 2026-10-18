// One-time browser-local input, persisted as an absolute UTC deadline.
export function localDeadline(epoch: number): string {
    if (!epoch) return '';
    const d = new Date(epoch * 1000);
    const pad = (v: number) => String(v).padStart(2, '0');
    return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

export function utcDeadline(local: string): number {
    if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}$/.test(local)) return 0;
    const epoch = new Date(local).getTime() / 1000;
    // Reject invalid dates and nonexistent local DST times rather than silently shifting them.
    return Number.isInteger(epoch) && epoch >= 1577836800 && epoch <= 4294967295 && localDeadline(epoch) === local
        ? epoch
        : 0;
}
