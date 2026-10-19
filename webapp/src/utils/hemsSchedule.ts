export type StorageScheduleStep = {
    start: number;
    end: number;
    used_price_micros_per_kwh: number;
    action: string;
    reason?: string;
    battery_w: number;
    end_soc_pct: number;
};

export type StorageOperationBlock = StorageScheduleStep & {
    slots: number;
    min_price_micros_per_kwh: number;
    max_price_micros_per_kwh: number;
};

export type StorageTimelinePart = {
    start: number;
    end: number;
    action: string;
    battery_w: number;
    gap: boolean;
};

function valid(step: StorageScheduleStep) {
    return Number.isFinite(step.start) && Number.isFinite(step.end) && step.end > step.start;
}

function normalizedAction(action: string) {
    return action === 'stop' ? 'hold' : action;
}

export function operationBlocks(steps: StorageScheduleStep[]): StorageOperationBlock[] {
    const sorted = steps
        .filter(valid)
        .slice()
        .sort((a, b) => a.start - b.start || a.end - b.end);
    const result: StorageOperationBlock[] = [];
    for (const source of sorted) {
        const action = normalizedAction(source.action);
        const step = { ...source, action };
        const previous = result.at(-1);
        if (
            previous &&
            previous.end === step.start &&
            previous.action === step.action &&
            previous.battery_w === step.battery_w &&
            (previous.reason || '') === (step.reason || '')
        ) {
            previous.end = step.end;
            previous.end_soc_pct = step.end_soc_pct;
            previous.slots++;
            previous.min_price_micros_per_kwh = Math.min(
                previous.min_price_micros_per_kwh,
                step.used_price_micros_per_kwh
            );
            previous.max_price_micros_per_kwh = Math.max(
                previous.max_price_micros_per_kwh,
                step.used_price_micros_per_kwh
            );
            continue;
        }
        result.push({
            ...step,
            slots: 1,
            min_price_micros_per_kwh: step.used_price_micros_per_kwh,
            max_price_micros_per_kwh: step.used_price_micros_per_kwh,
        });
    }
    return result;
}

export function currentScheduleStep(steps: StorageScheduleStep[], now: number) {
    return steps.find((step) => valid(step) && step.start <= now && now < step.end);
}

export function nextOperationBlock(blocks: StorageOperationBlock[], now: number) {
    const current = blocks.findIndex((block) => block.start <= now && now < block.end);
    if (current >= 0) return blocks[current + 1];
    return blocks.find((block) => block.start > now);
}

export function timelineParts(steps: StorageScheduleStep[], start: number, end: number): StorageTimelinePart[] {
    if (!(end > start)) return [];
    const sorted = steps
        .filter(valid)
        .slice()
        .sort((a, b) => a.start - b.start || a.end - b.end);
    const result: StorageTimelinePart[] = [];
    let cursor = start;
    for (const step of sorted) {
        if (step.end <= start || step.start >= end) continue;
        const from = Math.max(start, step.start);
        const until = Math.min(end, step.end);
        if (from > cursor) result.push({ start: cursor, end: from, action: 'gap', battery_w: 0, gap: true });
        const effectiveFrom = Math.max(cursor, from);
        if (until <= effectiveFrom) continue;
        const action = normalizedAction(step.action);
        const previous = result.at(-1);
        if (
            previous &&
            !previous.gap &&
            previous.end === effectiveFrom &&
            previous.action === action &&
            previous.battery_w === step.battery_w
        ) {
            previous.end = until;
        } else {
            result.push({
                start: effectiveFrom,
                end: until,
                action,
                battery_w: step.battery_w,
                gap: false,
            });
        }
        cursor = Math.max(cursor, until);
    }
    if (cursor < end) result.push({ start: cursor, end, action: 'gap', battery_w: 0, gap: true });
    return result;
}

export function socAt(
    steps: StorageScheduleStep[],
    initialSoc: number | undefined,
    horizonStart: number | undefined,
    time: number
): number | undefined {
    const sorted = steps
        .filter(valid)
        .slice()
        .sort((a, b) => a.start - b.start || a.end - b.end);
    let value = Number.isFinite(initialSoc) && horizonStart != null && time >= horizonStart ? initialSoc : undefined;
    for (const step of sorted) {
        if (step.end > time) break;
        if (Number.isFinite(step.end_soc_pct)) value = step.end_soc_pct;
    }
    return value;
}
