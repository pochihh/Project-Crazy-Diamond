import { deployedNodes, dspView, peakLevel, topicRows } from './instruments';
import './style.css';
import {dataTools, type DataState} from './data';
import { command } from './command';
import { PositionPlot } from './plot';
import { layout, meter } from './layout';
import { controllerPanel, type ControllerState } from './controllers';
import { defaults, readSettings, type Settings } from './settings';
type State = ControllerState & DataState & {
    protocol_version: number;
    control_rate_hz: number;
    dsp: {
        timestamp_us: number; sequence: number; session: number; command_sequence: number;
        state: number; arm_mask: number; fault: number; encoder_valid: number;
        position: number[]; reference: number[]; requested_duty: number[]; duty: number[];
        digital_in: number; digital_out: number; analog_valid: number; analog_out_valid: number;
        analog_in: number[]; analog_out: number[];
        invalid_frames: number; rejected_commands: number; command_timeout_count: number;
        encoder_errors: number; cpu2_max_poll_us: number;
    };
    feedback_age_ms: number;
    input_rejections: number;
    input_error: string;
    input_topic: string;
    input_channel: number;
    bridge_alive: boolean;
    connected: boolean;
    mock: boolean;
    system_enabled: boolean;
    axis_enabled: boolean[];
    project_id: string;
    control_mode: number[];
    pwm_target: number[];
    counts_per_mm: number[];
    minimum_mm: number[];
    maximum_mm: number[];
    position_mm: number[];
    target_mm: number[];
    reference_mm: number[];
    zero_counts: number[];
    duty_limit: number[];
    fault: string;
    sample_count: number;
    timestamp_us: number;
    deadline_misses: number;
    telemetry_errors: number;
    dropped_samples: number;
    max_cycle_us: number;
    max_lateness_us: number;
    recording: boolean;
    recording_file: string;
    recorded_samples: number;
    recording_error: string;
    ui_sequence?: number;
    ui_instance?: string;
};
type Project = {
    id: string;
    name: string;
    description?: string;
    model_status?: string;
    axes: {
        name: string;
        description?: string;
        motor?: string;
        encoder?: string;
    }[];
};
const $ = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;
const text = (id: string, value: string | number) => { const element = $(id), s = String(value); if (element.textContent !== s)
    element.textContent = s; };
const escape = (value: unknown) => String(value).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]!));
const inputValue = (id: string, value: string) => { const el = $<HTMLInputElement>(id); if (el.value !== value)
    el.value = value; };
$('app').innerHTML = layout;
const controllers = controllerPanel();
const data=dataTools();
let project: Project = { id: 'heart_phantom', name: 'Heart Phantom', axes: [{ name: 'Axis 0' }, { name: 'Axis 1' }] };
let settings: Settings;
try {
    settings = readSettings(localStorage.getItem('pcd-web-settings'));
}
catch {
    settings = { ...defaults };
}
let state: State | undefined;
let socket: WebSocket | undefined, socketConnected = false, reconnectTimer: ReturnType<typeof setTimeout>;
let activeTab = 'control', dirtyState = false, lastSample = -1, wasRecording = false;
let clockOffset = Infinity, latestFrameTime = 0, latestReceipt = 0, latestFresh = 0, lastResourceUpdate = 0;
let snapshotPending = false, lastSnapshotRequest = 0, lastPlotPaint = 0, lastSystemUpdate = 0, systemPending = false;
let lastInputRejections = 0;
let messageTimeout: ReturnType<typeof setTimeout>;
const retiredStreams = new Set<string>();
const plots = [0, 1].map(a => new PositionPlot($<HTMLCanvasElement>(`plot-${a}`)));
const plotStartSequences = [0, 0];
const pendingTargets = new Map<number, {
    operation: string;
    value: number;
}>();
let sendingAxis = -1;
const activeInputs = new Set<number>(), dirtyTargets = new Set<number>();
const topicSockets = new Map<string, WebSocket>();
let topicSignature = '';
let perfStart = performance.now(), frameCount = 0, plotCount = 0, messageCount = 0, maxPaintMs = 0;
function message(value: string, error = false) { text('message', value); $('message').hidden = !value; $('message').className = error ? 'error' : ''; clearTimeout(messageTimeout); messageTimeout = setTimeout(() => $('message').hidden = true, 7000); }
async function act(operation: string, axis = 0, value = 0, enabled = false) { try {
    await command(operation, axis, value, enabled);
}
catch (e) {
    message(e instanceof Error ? e.message : String(e), true);
} }
async function getJSON(path: string) { const controller = new AbortController(); const timer = setTimeout(() => controller.abort(), 2500); try {
    const r = await fetch(path, { signal: controller.signal, cache: 'no-store' });
    if (!r.ok)
        throw new Error(`HTTP ${r.status}`);
    return await r.json();
}
finally {
    clearTimeout(timer);
} }
const meters = new Map<string, {element: HTMLElement; value: number; peak: number; at: number}>();
function setMeter(id: string, value: number | null, label?: string) {
    const element = $(id), now = performance.now();
    const current = Math.min(100, Math.max(0, value ?? 0));
    const previous = meters.get(id);
    const peak = previous && value !== null ? peakLevel(previous.peak, current, now - previous.at) : current;
    meters.set(id,{element, value:current, peak, at:now});
    const reading = label ?? (value === null ? '—' : `${value.toFixed(1)}%`);
    text(`${id}-value`, reading);
    element.setAttribute('aria-valuenow', String(current));
    element.setAttribute('aria-valuetext', reading);
    element.style.setProperty('--level', current + '%');
    element.style.setProperty('--peak', peak + '%');
}
function decayMeters(now: number) {
    for (const m of meters.values()) {
        if (m.peak <= m.value || now <= m.at) continue;
        m.peak = peakLevel(m.peak, m.value, now - m.at);
        m.at = now;
        m.element.style.setProperty('--peak', m.peak + '%');
    }
}
function disclosure(id: string, content: string) {
    $(id).onclick = () => {
        const body = $(content);
        body.hidden = !body.hidden;
        $(id).setAttribute('aria-expanded', String(!body.hidden));
    };
}
disclosure('dsp-expand','dsp-details');
for(let axis=0;axis<2;axis++) disclosure(`expand-${axis}`,`axis-body-${axis}`);
function bounds(a: number) { return state?.control_mode?.[a] === 1 ? [-100 * (state.duty_limit[a] ?? 1), 100 * (state.duty_limit[a] ?? 1)] : [state?.minimum_mm?.[a] ?? -50, state?.maximum_mm?.[a] ?? 50]; }
async function flushTargets() { if (sendingAxis >= 0)
    return; try {
    while (pendingTargets.size) {
        const [a, t] = pendingTargets.entries().next().value!;
        pendingTargets.delete(a);
        sendingAxis = a;
        await command(t.operation, a, t.value);
    }
}
catch (e) {
    pendingTargets.clear();
    message(String(e), true);
}
finally {
    sendingAxis = -1;
} }
function target(a: number, value: number) { const [minimum, maximum] = bounds(a); const pwm = state?.control_mode?.[a] === 1; if (!Number.isFinite(value) || (pwm && (value < minimum || value > maximum))) {
    message(pwm ? `Duty must be between ${minimum} and ${maximum}.` : 'Position must be finite.', true);
    return;
} inputValue(`target-${a}`, value.toFixed(1)); const slider = $<HTMLInputElement>(`slider-${a}`); if (Number(slider.value) !== value)
    slider.value = String(value); dirtyTargets.delete(a); pendingTargets.set(a, { operation: pwm ? 'pwm' : 'target', value: pwm ? value / 100 : value }); void flushTargets(); }
const dirtyAnalog = new Set<number>();
for (let i=0;i<4;i++) $<HTMLInputElement>(`do-set-${i}`).onchange=e=>void act('digital',i,(e.target as HTMLInputElement).checked?1:0);
for (let i=0;i<2;i++) {
    $(`ao-set-${i}`).addEventListener('input',()=>dirtyAnalog.add(i));
    $(`ao-apply-${i}`).onclick=async()=>{await act('analog',i,Number($<HTMLInputElement>(`ao-set-${i}`).value));dirtyAnalog.delete(i);};
}
for (let a = 0; a < 2; a++) {
    const slider = $<HTMLInputElement>(`slider-${a}`);
    slider.addEventListener('pointerdown', () => activeInputs.add(a));
    slider.addEventListener('input', () => target(a, Number(slider.value)));
    for (const event of ['change', 'pointercancel', 'blur'])
        slider.addEventListener(event, () => activeInputs.delete(a));
    $(`apply-${a}`).onclick = () => target(a, Number($<HTMLInputElement>(`target-${a}`).value));
    $(`target-${a}`).addEventListener('keydown', e => { if (e.key === 'Enter')
        target(a, Number((e.target as HTMLInputElement).value)); });
    $(`target-${a}`).addEventListener('input', () => dirtyTargets.add(a));
    $<HTMLInputElement>(`enable-${a}`).onchange = e => void act('axis', a, 0, (e.target as HTMLInputElement).checked);
    $(`zero-${a}`).onclick = () => void act('zero', a);
    $<HTMLSelectElement>(`mode-${a}`).onchange = async (e) => { pendingTargets.delete(a); activeInputs.delete(a); dirtyTargets.delete(a); await act('mode', a, Number((e.target as HTMLSelectElement).value)); dirtyState = true; };

}
for (const event of ['pointerup', 'pointercancel', 'blur'])
    window.addEventListener(event, () => activeInputs.clear());
$('dsp-start').onclick = () => void act('system', 0, 0, true);
$('host-clear').onclick = () => void act('clear');
$('stop').onclick = () => { pendingTargets.clear(); activeInputs.clear(); void act('stop'); };
$('dsp-stop').onclick = () => { pendingTargets.clear(); void act('dsp_disarm'); };
$('dsp-clear').onclick = () => void act('dsp_clear');
function closeTopics() { const sockets = [...topicSockets.values()]; topicSockets.clear(); sockets.forEach(ws => ws.close()); }
document.querySelectorAll<HTMLButtonElement>('[data-tab]').forEach(button => button.onclick = () => {
    activeTab = button.dataset.tab!;
    data.tab(activeTab);
    document.querySelectorAll<HTMLElement>('[data-tab]').forEach(b => { const on = b === button; b.classList.toggle('active', on); if (on)
        b.setAttribute('aria-current', 'page');
    else
        b.removeAttribute('aria-current'); });
    document.querySelectorAll<HTMLElement>('.view').forEach(v => v.hidden = v.id !== activeTab);
    dirtyState = true;
    lastResourceUpdate = 0;
    closeTopics();
    $('workspace').scrollTop = 0;
    window.scrollTo(0,0);
    if (activeTab === 'system') {
        void updateSystem();
        document.querySelectorAll<HTMLDetailsElement>('.topic[open]').forEach(openTopic);
    }
});
function row(table: HTMLElement, cells: string[]) { const tr = document.createElement('tr'); cells.forEach(value => { const td = document.createElement('td'); td.textContent = value; tr.append(td); }); table.append(tr); }
function render(s: State) {
    const live = (socketConnected || performance.now() - latestReceipt < 1000) && s.bridge_alive && s.connected;
    text('source', s.mock ? 'SIMULATED HARDWARE' : 'DSP 00 / PHYSICAL');
    $('fault').hidden = !s.fault;
    if (s.fault && $('fault').dataset.error !== s.fault) {
        $('fault').dataset.error = s.fault;
        $('fault').replaceChildren(document.createTextNode(`${s.fault} `));
        $('fault').append(' · See DSP actions on Dashboard');
    }
    for (let a = 0; a < 2; a++) {
        const enabled = !!s.system_enabled && !!s.axis_enabled?.[a] && !s.fault, pwm = s.control_mode?.[a] === 1;
        const pos = s.position_mm?.[a] ?? 0, t = pwm ? (s.pwm_target?.[a] ?? 0) * 100 : s.target_mm?.[a] ?? 0;
        text(`position-${a}`, live ? pos.toFixed(2) : '—');
        text(`axis-status-${a}`, enabled ? (pwm ? 'PWM OUTPUT' : 'HOLD / TRACK') : 'DISABLED');
        $(`axis-status-${a}`).classList.toggle('on', enabled);
        {
            const input = $<HTMLInputElement>(`enable-${a}`);
            input.checked = !!s.axis_enabled?.[a];
            input.disabled = !live || !!s.fault;
        }
        $<HTMLSelectElement>(`mode-${a}`).value = pwm ? '1' : '0';
        $<HTMLSelectElement>(`mode-${a}`).disabled = !live || enabled || !s.control_mode;
        $<HTMLButtonElement>(`zero-${a}`).disabled = !live || enabled;
        const [minimum, maximum] = bounds(a);
        for (const id of [`slider-${a}`, `target-${a}`]) {
            const input = $<HTMLInputElement>(id);
            input.disabled = !live;
            if(pwm || id.startsWith('slider-')) { input.min = String(minimum); input.max = String(maximum); }
            else { input.removeAttribute('min'); input.removeAttribute('max'); }
            input.setAttribute('aria-label', `Axis ${a} target ${pwm ? 'duty percent' : 'position mm'}`);
        }
        $<HTMLButtonElement>(`apply-${a}`).disabled = !live;
        text(`target-label-${a}`, pwm ? 'Duty / %' : 'Target / mm');
        for (let i = 0; i < 5; i++) {
            const value = Number((minimum + (maximum - minimum) * i / 4).toFixed(2));
            text(`ruler-${a}-${i}`, `${i === 4 && value > 0 ? '+' : ''}${value}${i === 4 ? (pwm ? ' %' : ' mm') : ''}`);
        }
        const marker = $(`marker-${a}`);
        const actual = pwm ? 100 * (s.dsp.duty?.[a] ?? 0) : pos;
        marker.hidden = !live;
        marker.title = pwm ? 'Reported output duty' : 'Measured position';
        marker.style.left = `${Math.max(0, Math.min(100, (actual - minimum) / (maximum - minimum) * 100))}%`;
        if (!activeInputs.has(a) && !pendingTargets.has(a) && sendingAxis !== a) {
            inputValue(`slider-${a}`, String(t));
            if (document.activeElement !== $(`target-${a}`) && !dirtyTargets.has(a))
                inputValue(`target-${a}`, t.toFixed(1));
        }
        const duty = 100 * (s.dsp.duty?.[a] ?? 0);
        setMeter(`duty-meter-${a}`, live ? Math.abs(duty) : null, live ? `${duty.toFixed(1)}%` : '—');
        text(`error-label-${a}`, pwm ? 'Duty error' : 'Error');
        text(`error-${a}`, live ? (pwm ? `${(t-duty).toFixed(1)}%` : `${(t - pos).toFixed(2)} mm`) : '—');
        text(`target-read-${a}`, live ? t.toFixed(2) + (pwm ? '%' : ' mm') : '—');
        $(`target-legend-${a}`).hidden = pwm;
        plots[a].showTarget = !pwm;

    }
    controllers.render(s,live);
    text('dsp-rate', `${(s.control_rate_hz ?? 0).toLocaleString()} Hz`);
    text('dsp-link', live ? 'SPI / 10 MHz' : 'OFFLINE');
    text('dsp-frames', s.dsp.sequence.toLocaleString());
    const dspState = dspView(live, s.dsp.session, s.dsp.state, s.dsp.fault);
    text('dsp-status', dspState);
    $('dsp-status').dataset.state = dspState;
    $('dsp-start').hidden = dspState !== 'IDLE' || !!s.fault || s.system_enabled;
    $('dsp-stop').hidden = !live || !(s.system_enabled || s.dsp.arm_mask || dspState === 'RUNNING');
    $('dsp-clear').hidden = dspState !== 'FAULT' || !!s.system_enabled || !!s.dsp.arm_mask;
    $('host-clear').hidden = !live || !s.fault || !!s.dsp.fault || !!s.system_enabled || !!s.dsp.arm_mask;
    text('dsp-host-enabled', s.system_enabled ? 'Enabled' : 'Disabled');
    text('dsp-host-errors', s.telemetry_errors);
    text('dsp-freshness', live ? `Live DSP feedback · ${s.feedback_age_ms.toFixed(1)} ms old` : 'DSP feedback unavailable · values below are cached');
    text('dsp-session', s.dsp.session.toString());
    text('dsp-ack', s.dsp.command_sequence.toString());
    text('dsp-arm', `0x${s.dsp.arm_mask.toString(16)}`);
    const faults=[[1,'Command timeout'],[2,'Invalid command'],[4,'Encoder unavailable']].filter(([bit])=>s.dsp.fault&Number(bit)).map(([,name])=>name);
    text('dsp-fault', `0x${s.dsp.fault.toString(16)}${faults.length?' / '+faults.join(', '):' / None'}`);
    text('dsp-invalid', s.dsp.invalid_frames);
    text('dsp-rejected', s.dsp.rejected_commands);
    text('dsp-timeouts', s.dsp.command_timeout_count);
    text('dsp-encoder-errors', s.dsp.encoder_errors);
    text('dsp-encoder-valid', `0x${s.dsp.encoder_valid.toString(16)}`);
    text('dsp-poll', `${s.dsp.cpu2_max_poll_us} µs`);
    $<HTMLButtonElement>('dsp-stop').disabled=!s.bridge_alive;
    $<HTMLButtonElement>('dsp-clear').disabled=!live || s.system_enabled || s.dsp.arm_mask!==0;
    text('record-status', s.recording ? '● RECORDING' : 'IDLE');
    $('record-status').classList.toggle('recording', s.recording);
    $<HTMLButtonElement>('record-start').disabled = !live || s.recording;
    $<HTMLButtonElement>('record-stop').disabled = !s.bridge_alive || !s.recording;
    text('record-count', `${(s.recorded_samples ?? 0).toLocaleString()} samples · ${s.dropped_samples ?? 0} dropped`);
    text('record-error', s.recording_error ?? '');
    if (wasRecording && !s.recording)
        void loadRecordings();
    wasRecording = s.recording;
    data.render(s,live);
    if (activeTab === 'resources' && performance.now() - lastResourceUpdate >= 200) {
        lastResourceUpdate = performance.now();
        renderResources(s, live);
    }
}
function renderResources(s: State, live: boolean) {
    const rows = $('resource-rows');
    rows.replaceChildren();
    for (let a = 0; a < 2; a++) {
        const meta = project.axes[a], tr = document.createElement('tr'), enabled = s.system_enabled && s.axis_enabled[a] && !s.fault;
        tr.innerHTML = `<td>${escape(meta.name)}<small>DSP 00 / channel ${a}</small></td><td>${escape(meta.motor ?? `DC motor ${a}`)}<small>${escape(meta.encoder ?? `eQEP${a + 1}`)}</small></td><td>${(s.counts_per_mm?.[a] ?? 321.8467).toFixed(3)} counts/mm<small>${s.minimum_mm?.[a] ?? -50} … ${s.maximum_mm?.[a] ?? 50} mm</small></td><td>${live ? s.position_mm[a].toFixed(3) : '—'} mm<small>${live ? s.dsp.position[a] : '—'} counts</small></td><td>${meter(`resource-duty-${a}`, 'Output duty')}</td><td><span class="badge ${enabled ? 'on' : ''}">${enabled ? 'ENABLED' : 'DISABLED'}</span><small>${s.control_mode?.[a] === 1 ? 'PWM' : 'Position'}</small></td>`;
        rows.append(tr);
        const duty = s.dsp.duty[a] * 100;
        setMeter(`resource-duty-${a}`, live ? Math.abs(duty) : null, live ? `${duty.toFixed(1)}%` : '—');
    }
    const transport = $('transport-rows');
    transport.replaceChildren();
    for (const cells of [['Device', 'TMS320F28379D / CPU1 + CPU2'], ['Interface', 'SPI mode 1 · 10 MHz · 192 bytes/frame'], ['Protocol', `v${s.protocol_version} · acknowledged command ${s.dsp.command_sequence}`], ['Host loop', `${s.control_rate_hz.toLocaleString()} Hz · dedicated RT thread`], ['DSP timestamp', `${s.dsp.timestamp_us} µs / raw 32-bit counter`], ['Recording timestamp', `${s.timestamp_us} µs / host-unwrapped DSP counter`], ['Communication health', `${s.telemetry_errors} telemetry errors · ${s.dropped_samples} dropped samples`], ['DSP-confirmed state', `Armed mask 0x${s.dsp.arm_mask.toString(16)} · fault 0x${s.dsp.fault.toString(16)}`], ['Encoder validity', `Channels 0–5 mask: 0x${s.dsp.encoder_valid.toString(16)} · CPU2 max polling gap ${s.dsp.cpu2_max_poll_us} µs`], ['Encoder counts (0–5)', s.dsp.position?.map((n,i)=>((s.dsp.encoder_valid>>i)&1)?String(n):'Unavailable').join(' / ') ?? '—'], ['DSP diagnostics', `${s.dsp.invalid_frames} invalid frames · ${s.dsp.rejected_commands} rejected commands · ${s.dsp.command_timeout_count} command timeouts`]])
        row(transport, cells);
    const outputsEnabled=live && s.system_enabled && !s.fault;
    for(let i=0;i<4;i++) {
        text(`di-${i}`,live?((s.dsp.digital_in>>i)&1):'—');text(`do-${i}`,live?((s.dsp.digital_out>>i)&1):'—');
        const input=$<HTMLInputElement>(`do-set-${i}`);input.disabled=!outputsEnabled;input.checked=!!((s.dsp.digital_out>>i)&1);
    }
    for(let i=0;i<6;i++) text(`ai-${i}`,live&&((s.dsp.analog_valid>>i)&1)?s.dsp.analog_in[i]:'Unavailable');
    for(let i=0;i<2;i++) {
        text(`ao-${i}`,live&&((s.dsp.analog_out_valid>>i)&1)?s.dsp.analog_out[i]:'—');
        const input=$<HTMLInputElement>(`ao-set-${i}`);input.disabled=!outputsEnabled;
        $<HTMLButtonElement>(`ao-apply-${i}`).disabled=!outputsEnabled;
        if(document.activeElement!==input && !dirtyAnalog.has(i))inputValue(`ao-set-${i}`,String(s.dsp.analog_out[i]??0));
    }
}
function receive(s: State) {
    const now = performance.now(), time = s.timestamp_us / 1000;
    if (s.ui_instance && retiredStreams.has(s.ui_instance))
        return;
    if (s.ui_instance === state?.ui_instance && (s.ui_sequence ?? 0) < (state?.ui_sequence ?? 0)) {
        // Preserve delayed history without rolling live controls/status backwards.
        if (s.connected && s.sample_count < lastSample && time <= latestFrameTime)
            plots.forEach((plot, a) => {
                if ((s.ui_sequence ?? 0) >= plotStartSequences[a] && s.control_mode?.[a] === state?.control_mode?.[a])
                    plot.add(time, s.target_mm[a], s.position_mm[a]);
            });
        return;
    }
    if (state && s.ui_instance === state.ui_instance && s.input_rejections > lastInputRejections)
        message(`${s.input_topic} channel ${s.input_channel}: ${s.input_error}`, true);
    lastInputRejections = s.input_rejections;
    const newStream = !!state?.ui_instance && s.ui_instance !== state.ui_instance;
    if (newStream)
        retiredStreams.add(state!.ui_instance!);
    if (newStream || s.sample_count < lastSample || time < latestFrameTime) {
        plots.forEach((p,a) => { p.clear(); plotStartSequences[a]=s.ui_sequence ?? 0; });
        clockOffset = Infinity;
    }
    if (s.connected && (newStream || s.sample_count !== lastSample)) {
        clockOffset = Math.min(clockOffset, now - time);
        latestFrameTime = time;
        latestFresh = now;
        messageCount++;
        plots.forEach((plot, a) => { if (state?.control_mode?.[a] !== s.control_mode?.[a]) {
            plot.clear(); plotStartSequences[a]=s.ui_sequence ?? 0;
        } plot.add(time, s.target_mm[a], s.position_mm[a]); });
    }
    lastSample = s.sample_count;
    latestReceipt = now;
    state = s;
    dirtyState = true;
}
async function refreshSnapshot() { snapshotPending = true; lastSnapshotRequest = performance.now(); const controller = new AbortController(); const timer = setTimeout(() => controller.abort(), 800); try {
    const r = await fetch('/api/state', { signal: controller.signal, cache: 'no-store' });
    if (r.ok) {
        const s = await r.json();
        if (s.position_mm)
            receive(s);
    }
}
catch { }
finally {
    clearTimeout(timer);
    snapshotPending = false;
} }
function animate(now: number) {
    const start = performance.now();
    frameCount++;
    decayMeters(now);
    const stallThreshold = Math.max(150, 3000 / settings.streamHz);
    if (now - latestFresh > stallThreshold && now - lastSnapshotRequest > stallThreshold && !snapshotPending)
        void refreshSnapshot();
    if (state) {
        if (dirtyState) {
            render(state);
            dirtyState = false;
        }
        const live = (socketConnected || now - latestReceipt < 1000) && state.bridge_alive && state.connected;
        const age = Math.max(now - latestReceipt, now - clockOffset - latestFrameTime), delayed = live && age > Math.max(250, stallThreshold);
        text('connection', live ? (delayed ? '◷ DATA DELAYED' : '● CONNECTED') : '○ OFFLINE');
        $('connection').classList.toggle('online', live && !delayed);
        if (activeTab === 'control' && now - lastPlotPaint >= 1000 / settings.plotFps - .5) {
            plots.forEach(p => p.draw(now - clockOffset - 100));
            lastPlotPaint += Math.max(1, Math.floor((now - lastPlotPaint + .5) / (1000 / settings.plotFps))) * (1000 / settings.plotFps);
            plotCount++;
        }
    }
    maxPaintMs = Math.max(maxPaintMs, performance.now() - start);
    if (now - perfStart >= 1000) {
        if (settings.showPerformance) {
            const seconds = (now - perfStart) / 1000;
            const age = Number.isFinite(clockOffset) ? Math.max(0, now - clockOffset - latestFrameTime) : 0;
            text('performance', `DISPLAY ${(frameCount / seconds).toFixed(0)} fps · PLOT ${(plotCount / seconds).toFixed(0)} fps · DATA ${(messageCount / seconds).toFixed(0)} Hz · PAINT ${maxPaintMs.toFixed(1)} ms · AGE ${age.toFixed(0)} ms`);
        }
        perfStart = now;
        frameCount = plotCount = messageCount = 0;
        maxPaintMs = 0;
    }
    requestAnimationFrame(animate);
}
async function loadRecordings() { try {
    const entries: {
        name: string;
        bytes: number;
        active: boolean;
    }[] = await getJSON('/api/recordings');
    const container = $('recordings');
    container.replaceChildren();
    for (const entry of entries.slice(0, 10)) {
        const line = document.createElement('div');
        line.className = 'file-row';
        const label = document.createElement('span');
        label.textContent = `${entry.name} · ${(entry.bytes / 1024).toFixed(1)} KB`;
        line.append(label);
        if (!entry.active) {
            const link = document.createElement('a');
            link.textContent = 'DOWNLOAD CSV ↗';
            link.href = `/api/recordings/${encodeURIComponent(entry.name)}`;
            link.download = entry.name;
            line.append(link);
        }
        container.append(line);
    }
}
catch {
    message('Could not load recordings.', true);
} }
const bytes = (n: number) => `${(n / 1024 ** 3).toFixed(1)} GiB`;
function openTopic(details: HTMLDetailsElement) {
    const name = details.dataset.topic!;
    if (activeTab !== 'system' || !details.open || topicSockets.has(name))
        return;
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws/topics?name=${encodeURIComponent(name)}`);
    topicSockets.set(name, ws);
    const status = details.querySelector<HTMLElement>('.topic-stream-status')!, body = details.querySelector('tbody')!;
    status.textContent = 'Connecting to topic…';
    ws.onmessage = event => { if (topicSockets.get(name) !== ws)
        return; const payload = JSON.parse(event.data); status.textContent = payload.error ?? `VIEW / 10 Hz · ${payload.count} received · ${payload.age_ms === null ? 'waiting for publisher' : `${payload.age_ms} ms since message`}`; topicRows(payload.data, body); };
    ws.onclose = () => { if (topicSockets.get(name) !== ws)
        return; topicSockets.delete(name); status.textContent = 'Stream disconnected'; setTimeout(() => { if (details.isConnected && details.open && activeTab === 'system')
        openTopic(details); }, 1500); };
    ws.onerror = () => ws.close();
}
function renderTopics(topics: {
    name: string;
    types: string[];
}[]) {
    const signature = JSON.stringify(topics);
    if (signature === topicSignature)
        return;
    topicSignature = signature;
    const container = $('topics');
    const names = new Set(topics.map(t => t.name));
    for (const d of Array.from(container.querySelectorAll<HTMLDetailsElement>('details')))
        if (!names.has(d.dataset.topic!)) {
            topicSockets.get(d.dataset.topic!)?.close();
            topicSockets.delete(d.dataset.topic!);
            d.remove();
        }
    for (const topic of topics) {
        if (Array.from(container.children).some(el => (el as HTMLElement).dataset.topic === topic.name))
            continue;
        const details = document.createElement('details');
        details.className = 'topic';
        details.dataset.topic = topic.name;
        details.innerHTML = `<summary><strong>${escape(topic.name)}</strong><small>${escape(topic.types.join(', '))}</small></summary><div class="topic-stream-status">Expand to subscribe</div><table class="topic-table"><tbody></tbody></table>`;
        details.addEventListener('toggle', () => { if (details.open)
            openTopic(details);
        else {
            const ws = topicSockets.get(topic.name);
            topicSockets.delete(topic.name);
            ws?.close();
        } });
        container.append(details);
    }
}
async function updateSystem() {
    if (systemPending)
        return;
    systemPending = true;
    lastSystemUpdate = performance.now();
    try {
        const info = await getJSON('/api/system'), usage = info.usage ?? {};
        setMeter('host-cpu', usage.cpu_percent ?? null);
        text('host-cpu-detail', `Load ${info.load_average.map((n: number) => n.toFixed(2)).join(' / ')}${usage.temperature_c === null || usage.temperature_c === undefined ? '' : ` · ${usage.temperature_c.toFixed(1)} °C`}`);
        for (const key of ['memory', 'disk']) {
            const value = usage[key];
            setMeter(`host-${key}`, value?.percent ?? null);
            text(`host-${key}-detail`, value ? `${bytes(value.used)} / ${bytes(value.total)}` : 'Usage unavailable');
        }
        setMeter('host-gpu', usage.gpu_percent ?? null);
        text('host-gpu-detail', usage.gpu_percent === null || usage.gpu_percent === undefined ? 'Utilisation not exposed by this driver' : 'Driver-reported GPU utilisation');
        text('host-platform', `ROS 2 ${info.ros_distro} · ${info.kernel}`);
        const cores: (number | null)[] = usage.cpu_cores ?? [];
        const coreList = $('host-cores');
        if (coreList.children.length !== cores.length) {
            coreList.innerHTML = cores.map((_, i) => `<div class="core-line"><div id="core-tags-${i}" class="core-tags"></div>${meter(`core-${i}`, `CPU ${i}`)}</div>`).join('');
        }
        cores.forEach((value, i) => {
            const pinned = i === 3 && state?.bridge_alive && !state.mock && state.fault !== 'Real-time scheduling or memory setup failed';
            text(`core-tags-${i}`, [(usage.isolated_cpus ?? []).includes(i) ? 'ISOLATED' : '', pinned ? 'RT PINNED' : ''].filter(Boolean).join(' · '));
            setMeter(`core-${i}`, value);
        });
        const nodes = $('nodes');
        nodes.replaceChildren();
        for (const name of info.nodes as string[]) {
            const [pkg, role] = deployedNodes[name] ?? ['—', 'Discovered ROS node'];
            row(nodes, ['/' + name, pkg, role]);
        }
        renderTopics(info.topics);
    }
    catch {
        text('host-platform', 'Host status unavailable — retrying');
    }
    finally {
        systemPending = false;
    }
}
function resizePlots() {
    const style = getComputedStyle(document.documentElement);
    const palette = {ink:style.getPropertyValue('--ink'), grid:style.getPropertyValue('--line'),
        text:style.getPropertyValue('--muted'), measured:style.getPropertyValue('--sage-ink'), font:'11px '+style.getPropertyValue('--mono')};
    data.setPlotStyle(palette);
    plots.forEach(p => {p.colors=palette; p.resize(devicePixelRatio);});
}
const darkMedia = matchMedia('(prefers-color-scheme: dark)');
function applySettings() { document.documentElement.dataset.theme = settings.theme === 'system' ? (darkMedia.matches ? 'dark' : 'light') : settings.theme; $('performance').hidden = !settings.showPerformance; text('system-refresh', `REFRESH / ${settings.statusSeconds} s`); resizePlots(); }
function saveSettings() { try {
    localStorage.setItem('pcd-web-settings', JSON.stringify(settings));
}
catch { } applySettings(); }
for (const [id, key] of [['setting-theme', 'theme'], ['setting-plot-fps', 'plotFps'], ['setting-stream-hz', 'streamHz'], ['setting-status-seconds', 'statusSeconds']] as const) {
    $<HTMLSelectElement>(id).value = String(settings[key]);
    $<HTMLSelectElement>(id).onchange = () => { settings = readSettings(JSON.stringify({ ...settings, [key]: key === 'theme' ? $<HTMLSelectElement>(id).value : Number($<HTMLSelectElement>(id).value) })); saveSettings(); if (key === 'streamHz')
        connect(); };
}
$<HTMLInputElement>('setting-performance').checked = settings.showPerformance;
$<HTMLInputElement>('setting-performance').onchange = () => { settings.showPerformance = $<HTMLInputElement>('setting-performance').checked; saveSettings(); };
darkMedia.addEventListener('change', applySettings);
async function loadProject() { try {
    project = await getJSON('/api/project');
    text('project-name', project.name);
    text('project-description', project.description ?? 'Project control dashboard');
    text('project-footer', `PROJECT / ${project.name.toUpperCase()}`);
    text('controller-source', `projects/${project.id}/controllers/*.yaml`);
    for (let a = 0; a < 2; a++) {
        document.querySelectorAll<HTMLElement>(`[data-axis-name="${a}"]`).forEach(el => el.textContent = project.axes[a].name);
        document.querySelectorAll<HTMLElement>(`[data-axis-description="${a}"]`).forEach(el => el.textContent = project.axes[a].description ?? 'Linear actuator');
    }
    dirtyState = true;
}
catch {
    message('Project metadata is unavailable.', true);
} }
function connect() { clearTimeout(reconnectTimer); const old = socket; socket = undefined; old?.close(); socketConnected = false; const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws?hz=${settings.streamHz}`); socket = ws; ws.onopen = () => { if (socket === ws)
    socketConnected = true; }; ws.onmessage = event => { if (socket !== ws)
    return; const data = JSON.parse(event.data); if (data.position_mm)
    receive(data); }; ws.onclose = () => { if (socket !== ws)
    return; socketConnected = false; dirtyState = true; reconnectTimer = setTimeout(connect, 1500); }; ws.onerror = () => ws.close(); }
const observer = new ResizeObserver(resizePlots);
for (let a = 0; a < 2; a++)
    observer.observe($(`plot-${a}`));
window.addEventListener('resize', resizePlots);
setInterval(() => { if (activeTab === 'system' && performance.now() - lastSystemUpdate >= settings.statusSeconds * 1000)
    void updateSystem(); }, 250);
applySettings();
void loadProject();
void loadRecordings();
connect();
requestAnimationFrame(animate);
