// Local input must resolve to a single selected UTC instant. Repeated DST times
// offer both offsets; nonexistent times have no candidate.
export function evDeadlineCandidates(local: string): number[] {
    if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}$/.test(local)) return [];
    const first = new Date(local).getTime() / 1000;
    const format = (epoch: number) => {
        const d = new Date(epoch * 1000);
        const pad = (n: number) => String(n).padStart(2, '0');
        return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
    };
    const result: number[] = [];
    for (let delta = -180; delta <= 180; delta += 15) {
        const epoch = first + delta * 60;
        if (Number.isInteger(epoch) && epoch >= 1577836800 && epoch <= 4294967295 && format(epoch) === local)
            result.push(epoch);
    }
    return result;
}
