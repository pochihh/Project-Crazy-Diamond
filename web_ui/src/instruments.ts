export function peakLevel(previous: number, current: number, elapsedMs: number) {
    return Math.max(current, previous - Math.max(0, elapsedMs) * 0.025);
}

export function dspView(live: boolean, session: number, raw: number, fault: number) {
    if (!live || !session) return 'INIT';
    if (fault || raw === 2) return 'FAULT';
    return raw === 0 ? 'IDLE' : raw === 1 ? 'RUNNING' : 'INIT';
}

// Nodes installed with PCD. The ROS graph gives node names but no package metadata.
export const deployedNodes: Record<string, [string, string]> = {
    bridge: ['bridge', '2 kHz control and SPI'],
    ui_bridge: ['ui_bridge', 'Web and telemetry'],
    data_server: ['data_tools', 'CSV library and import'],
    phantom_robot: ['phantom_robot', 'Heart Phantom experiments'],
};

export function topicRows(value: unknown, root: HTMLElement) {
    const rows = document.createDocumentFragment();
    function visit(key: string, item: unknown, depth: number) {
        const object = item !== null && typeof item === 'object';
        const simpleArray = Array.isArray(item) && item.every(v => v === null || typeof v !== 'object');
        const tr = document.createElement('tr');
        const label = document.createElement('th');
        label.scope = 'row';
        label.style.paddingInlineStart = `${12 + 12 * depth}px`;
        label.textContent = key;
        const cell = document.createElement('td');
        cell.textContent = simpleArray ? item.join(', ') : object ? '' : String(item);
        tr.append(label, cell); rows.append(tr);
        if (object && !simpleArray) {
            for (const [name, child] of Object.entries(item)) visit(name, child, depth + 1);
        }
    }
    for (const [key, item] of Object.entries(value ?? {})) visit(key, item, 0);
    root.replaceChildren(rows);
}

// Display precision only; command values and recorded samples stay unchanged.
export const displayValue=(value:number,digits=6)=>Math.abs(value)<1e-12?'0':value.toPrecision(digits);
