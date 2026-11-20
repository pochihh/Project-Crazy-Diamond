const storageKey = 'pcd-inspector-width';

export function inspectorWidths(viewport: number, preferred?: number) {
    const min = 208;
    const max = Math.max(min, Math.min(640, Math.floor(viewport * .48)));
    const initial = viewport <= 980 ? 240 : Math.round(Math.min(340, Math.max(248, viewport * .22)));
    const width = preferred !== undefined && Number.isFinite(preferred) && preferred > 0 ? preferred : initial;
    return {min, max, width: Math.round(Math.max(min, Math.min(width, max)))};
}

export function bindInspectorDivider(divider: HTMLElement) {
    const shell = divider.parentElement!;
    let preferred: number | undefined;
    try {
        const stored = Number(localStorage.getItem(storageKey));
        if (Number.isFinite(stored) && stored > 0) preferred = stored;
    } catch { /* Local storage can be unavailable in private contexts. */ }
    let drag: {id: number; x: number; width: number} | undefined;
    function apply(wanted = preferred) {
        const range = inspectorWidths(shell.clientWidth, wanted);
        shell.style.setProperty('--inspector-width', range.width + 'px');
        divider.setAttribute('aria-valuemin', String(range.min));
        divider.setAttribute('aria-valuemax', String(range.max));
        divider.setAttribute('aria-valuenow', String(range.width));
        divider.setAttribute('aria-valuetext', range.width + ' pixels');
        return range.width;
    }
    function commit(value: number) {
        preferred = apply(value);
        try { localStorage.setItem(storageKey, String(preferred)); } catch { /* Keep the session width. */ }
    }
    divider.onpointerdown = event => {
        if (drag || event.button !== 0) return;
        drag = {id: event.pointerId, x: event.clientX, width: apply()};
        divider.setPointerCapture(event.pointerId);
        shell.classList.add('resizing');
    };
    divider.onpointermove = event => {
        if (drag?.id === event.pointerId) apply(drag.width + drag.x - event.clientX);
    };
    divider.onpointerup = event => {
        if (drag?.id === event.pointerId) commit(drag.width + drag.x - event.clientX);
    };
    divider.onlostpointercapture = () => {
        drag = undefined;
        shell.classList.remove('resizing');
        apply();
    };
    divider.onkeydown = event => {
        const {min, max, width} = inspectorWidths(shell.clientWidth, preferred);
        const widths: Record<string,number> = {ArrowLeft: width + 16, ArrowRight: width - 16, Home: min, End: max};
        if (!(event.key in widths)) return;
        event.preventDefault();
        commit(widths[event.key]);
    };
    apply();
    window.addEventListener('resize', () => apply());
}
