import { controllerDiagram } from './controllerDiagram';
import { command, type ControllerCommand } from './command';
export type Profile = {id: string; type: string; parameter_names: string[]; parameter_values: number[]; slew_enabled: boolean; slew_rate_mm_s: number};
type ControllerType = {type: string; label: string; parameter_names: string[]; parameter_units: string[]; defaults: number[]; minimum: number[]; maximum: number[]};
export type ControllerState = {ui_instance?: string; ui_sequence?: number; controllers: Profile[]; controller_revision: number[]; control_rate_hz: number; program_name:string; runner_state:number; recording:boolean; recording_file:string; system_enabled: boolean; axis_enabled: boolean[]; duty_limit: number[]; control_mode: number[]; fault: string};
type Catalog = {types: ControllerType[]; profiles: Profile[]; errors: string[]};

export function controllerPanel() {
    const inspector = document.getElementById('inspector')!;
    const panel = document.createElement('section');
    panel.id = 'controller-panel'; panel.hidden = true;
    panel.setAttribute('aria-label', 'Axis controller settings');
    panel.innerHTML = `<div class="panel-head"><span id="controller-title">AXIS CONTROLLER</span></div><div class="axis-modes"></div><div class="settings-body"><label>Controller<select id="controller-type"></select></label><p>Change type while disabled. Parameters can be tuned while running.</p><details open><summary>Signal diagram</summary><div id="signal-diagram" class="controller-diagram"></div></details><label>Saved profile<select id="controller-profile"></select></label><div class="controller-actions"><button id="profile-load">LOAD / RELOAD</button><button id="profiles-refresh" class="secondary">REFRESH</button></div><p id="controller-profile-status"></p><div id="controller-parameters" class="controller-parameters"></div><p id="controller-readback" role="status"></p><label>Reference processing<select id="reference-type"><option value="passthrough">Passthrough / no shaping</option><option value="slew_limit">Slew limit</option></select></label><label>Maximum reference rate / mm/s<input id="reference-rate" type="number" min="0.000001" step="any" value="20"></label><button id="controller-apply">APPLY PARAMETERS</button><section id="step-actions" hidden><h3>Step experiment</h3><p>Holds 0 mm at the current axis origin. Start enables this axis and records baseline, injected step and recovery. Timing and amplitude are captured at Start.</p><div class="controller-actions"><button id="step-start">Start + record</button><button id="step-stop">Stop + save</button></div><p id="step-status" role="status"></p></section><div class="controller-actions"><button id="profile-save">SAVE PROFILE</button></div><label>New profile ID<input id="profile-new" placeholder="bench_pid_v2" maxlength="63" pattern="[a-z0-9_-]+"></label><button id="profile-create">SAVE AS NEW PROFILE</button><hr><label>Axis duty limit / 0–1<input id="controller-limit" type="number" min="0.000001" max="1" step="any"></label><button id="controller-limit-apply">SET DUTY LIMIT</button><p>Duty limit requires this axis disabled and stays in the axis configuration, outside the shared profile.</p><p id="controller-reference"></p><p id="controller-feedback" role="status"></p></div>`;
    inspector.append(panel);
    const el = <T extends HTMLElement = HTMLElement>(id: string) => panel.querySelector<T>(`#${id}`)!;
    const diagram=controllerDiagram(el('signal-diagram'));
    const type = el<HTMLSelectElement>('controller-type'), profile = el<HTMLSelectElement>('controller-profile');
    let axis = 0, current: ControllerState | undefined, live = false, busy = false, dirty = false, revision = -1;
    let catalog: Catalog = {types: [], profiles: [], errors: []};
    const feedback = (text: string) => { el('controller-feedback').textContent = text; };
    function options(select: HTMLSelectElement, values: [string,string][], selected: string) {
        select.replaceChildren(...values.map(([value,label]) => new Option(label,value)));
        select.value = selected;
    }
    function fields(kind: string, values: number[]) {
        const desc = catalog.types.find(t => t.type === kind);
        el('controller-parameters').replaceChildren();
        desc?.parameter_names.forEach((name,i) => {
            const label = document.createElement('label'); label.textContent = `${name.toUpperCase()} / ${desc.parameter_units[i]}`;
            const input = document.createElement('input'); input.type = 'number'; input.step = 'any';
            input.name = name; input.value = String(values[i] ?? desc.defaults[i]);
            input.min = String(desc.minimum[i]); input.max = String(desc.maximum[i]);
            input.addEventListener('input', () => { dirty = true; paint(); });
            label.append(input); el('controller-parameters').append(label);
        });
    }
    function update(s: ControllerState) {
        if(s.ui_instance === current?.ui_instance && (s.ui_sequence ?? 0) < (current?.ui_sequence ?? 0)) return;
        current = s;
    }
    function paint() {
        if(panel.hidden || !current?.controllers?.[axis]) return;
        const active = current.controllers[axis], enabled = current.system_enabled && current.axis_enabled[axis] && !current.fault;
        if(!dirty && revision !== current.controller_revision[axis] && catalog.types.length) {
            type.value = active.type; fields(active.type,active.parameter_values);
            profile.value = active.id; revision = current.controller_revision[axis];
            el<HTMLSelectElement>("reference-type").value=active.slew_enabled?"slew_limit":"passthrough";
            el<HTMLInputElement>("reference-rate").value=String(active.slew_rate_mm_s);
        }
        el('controller-readback').textContent = `Active read-back · revision ${current.controller_revision[axis]}: ` +
            active.parameter_names.map((name,i) => `${name.toUpperCase()} = ${active.parameter_values[i]}`).join(' · ') +
            (live ? '' : ' · STALE / disconnected');
        const kind=dirty?type.value:active.type;
        void diagram(kind,axis);
        const step=kind==='step_recorder';
        el('step-actions').hidden=!step;
        el('reference-type').closest('label')!.hidden=step;
        el('reference-rate').closest('label')!.hidden=step;
        const saved = catalog.profiles.find(p => p.id === active.id);
        const differs = !saved || saved.type !== active.type || saved.slew_enabled!==active.slew_enabled || saved.slew_rate_mm_s!==active.slew_rate_mm_s || saved.parameter_names.some((name,i) => active.parameter_values[active.parameter_names.indexOf(name)] !== saved.parameter_values[i]);
        el('controller-profile-status').textContent = dirty ? `Editing ${active.id || 'working copy'} — apply before saving.` :
            `${active.id || 'Working copy'} · ${differs ? 'active parameters differ from saved profile; Save or Reload explicitly' : 'matches saved profile'}`;
        for(const control of Array.from(panel.querySelectorAll<HTMLInputElement | HTMLButtonElement | HTMLSelectElement>('.settings-body :is(input, button, select)')))
            control.disabled = busy || !live;
        const running=current.runner_state===1 || current.runner_state===2;
        const ownRun=running && current.program_name===`Step Recorder / axis ${axis}`;
        el<HTMLButtonElement>('step-start').disabled=busy || !live || !!current.fault || current.recording || running || dirty || active.type!=='step_recorder';
        el<HTMLButtonElement>('step-stop').disabled=!live || !ownRun;
        el('step-status').textContent=ownRun?`Recording · ${current.recording_file}`:'';
        type.disabled = busy || !live || enabled;
        el<HTMLSelectElement>("reference-type").disabled=busy||!live||enabled;
        el<HTMLButtonElement>('controller-apply').disabled = busy || !live || !catalog.types.length;
        for(const id of ['profile-save','profile-create']) el<HTMLButtonElement>(id).disabled = busy || !live || dirty;
        el<HTMLButtonElement>('profile-load').disabled = busy || !live || !profile.value;
        el<HTMLButtonElement>('profile-save').disabled ||= !saved;
        const limit = el<HTMLInputElement>('controller-limit');
        limit.disabled = busy || !live || enabled;
        el<HTMLButtonElement>('controller-limit-apply').disabled = busy || !live || enabled;
        if(document.activeElement !== limit && !limit.dataset.dirty) limit.value = String(current.duty_limit[axis]);
        el('controller-reference').textContent = current.control_mode[axis] === 1 ? 'Direct PWM bypasses the position controller and reference ramp.' :
            step ? 'Step Recorder uses a fixed 0 mm reference.' : active.slew_enabled ? `Profile reference limit: ${active.slew_rate_mm_s} mm/s. No acceleration limit.` : 'Passthrough: reference reaches the controller unchanged.';
    }
    async function refresh() {
        const response = await fetch('/api/controllers', {cache: 'no-store', signal: AbortSignal.timeout(4000)});
        const data = await response.json();
        if(!response.ok) throw Error(data.detail ?? 'Controller catalog unavailable');
        catalog = data;
        const selected = profile.value || current?.controllers?.[axis]?.id || '';
        options(profile,catalog.profiles.map(p => [p.id,`${p.id} · ${p.type}`]),selected);
        options(type,catalog.types.map(t => [t.type,t.label]),dirty ? type.value : current?.controllers?.[axis]?.type ?? 'pid');
        if(catalog.errors.length) feedback(catalog.errors.join('; '));
        if(!dirty) revision = -1;
        paint();
    }
    async function send(operation: string, extra: ControllerCommand = {}, value = 0) {
        const sentAxis = axis;
        busy = true; paint(); feedback('Applying…');
        try {
            const result = await command(operation,axis,value,false,extra);
            update(result.state);
            if(selection !== sentAxis) return;
            if(operation==='step_start' || operation==='step_stop') {
                feedback(operation==='step_start'?'Step recording started.':'Step stopped. The CSV is available on Data.');
                return;
            }
            if(operation !== 'limit') { dirty = false; revision = -1; }
            if(operation.startsWith('profile_')) await refresh();
            feedback(operation === 'profile_save' || operation === 'profile_create' ? 'Profile saved. Other axes keep their active parameters until reloaded.' : `Applied and read back · command ${result.command_id}, revision ${result.controller_readback.controller_revision}.`);
        } catch(error) { if(selection === sentAxis) feedback(String(error)); }
        finally { busy = false; paint(); }
    }
    let selection: 'web' | number | null = null;
    const webButton = document.getElementById('settings-button')!;
    function select(next: typeof selection) {
        selection = next === selection ? null : next;
        const web = selection === 'web';
        webButton.classList.toggle('active', web);
        webButton.setAttribute('aria-pressed', String(web));
        document.getElementById('settings-panel')!.hidden = !web;
        panel.hidden = typeof selection !== 'number';
        for (const gear of Array.from(document.querySelectorAll<HTMLButtonElement>('.axis .gear'))) {
            const a = Number(gear.dataset.axis);
            const selected = a === selection;
            gear.setAttribute('aria-pressed',String(selected));
            gear.closest('.axis')!.classList.toggle('editing', selected);
            el(`mode-field-${a}`).hidden = !selected;
        }
        inspector.scrollTop = 0;
        if (typeof selection !== 'number') return;
        axis = selection;
        dirty = false;
        revision = -1;
        feedback('');
        el<HTMLInputElement>('controller-limit').removeAttribute('data-dirty');
        el('controller-title').textContent = `AXIS ${axis} / CONTROLLER`;
        paint();
        void refresh().catch(e => feedback(String(e)));
    }
    el('step-start').onclick=()=>void send('step_start',{controller_revision:current!.controller_revision[axis]});
    el('step-stop').onclick=()=>void send('step_stop');
    webButton.onclick = () => select('web');
    type.onchange = () => { dirty = true; fields(type.value,catalog.types.find(t => t.type === type.value)?.defaults ?? []); paint(); };
    el('controller-apply').onclick = () => {
        const parameters: Record<string,number> = {};
        for(const input of Array.from(panel.querySelectorAll<HTMLInputElement>('#controller-parameters input'))) {
            if(!input.value || !input.checkValidity() || !Number.isFinite(Number(input.value))) { feedback('Enter finite parameters within the displayed bounds.'); return; }
            parameters[input.name] = Number(input.value);
        }
        const rate=Number(el<HTMLInputElement>('reference-rate').value);
        if(!Number.isFinite(rate)||rate<=0){feedback('Reference rate must be positive and finite.');return;}
        void send('controller_apply',{controller_type: type.value,parameters,set_reference:true,slew_enabled:type.value!=='step_recorder' && el<HTMLSelectElement>('reference-type').value==='slew_limit',slew_rate_mm_s:rate});
    };
    for(const id of ['reference-type','reference-rate']) el(id).oninput=()=>{dirty=true;paint();};
    profile.onchange = paint;
    el('profile-load').onclick = () => void send('profile_load',{profile: profile.value});
    el('profile-save').onclick = () => void send('profile_save',{profile: current!.controllers[axis].id,controller_revision: current!.controller_revision[axis]});
    el('profile-create').onclick = () => {
        const id = el<HTMLInputElement>('profile-new').value;
        if(!/^[a-z0-9_-]{1,63}$/.test(id)) {feedback('Use 1–63 lowercase letters, digits, underscores or hyphens.');return;}
        void send('profile_create',{profile:id,controller_revision:current!.controller_revision[axis]});
    };
    el('profiles-refresh').onclick = () => { void refresh().catch(e => feedback(String(e))); };
    el<HTMLInputElement>('controller-limit').oninput = e => { (e.target as HTMLInputElement).dataset.dirty = 'true'; };
    el('controller-limit-apply').onclick = async () => {
        const input = el<HTMLInputElement>('controller-limit');
        if(!input.value || !input.checkValidity()) {feedback('Duty limit must be greater than zero and at most one.');return;}
        await send('limit',{},Number(input.value)); delete input.dataset.dirty; paint();
    };
    function addAxis(a: number) {
        panel.querySelector('.axis-modes')!.insertAdjacentHTML('beforeend', `<label id="mode-field-${a}" hidden>Axis control mode<select id="mode-${a}" aria-label="Axis ${a} control mode"><option value="0">Position</option><option value="1">PWM / duty</option></select></label>`);
        const gear = document.getElementById(`controller-open-${a}`)!;
        gear.dataset.axis = String(a);
        gear.onclick = () => select(a);
    }
    return {addAxis, clear: () => select(null), render(s: ControllerState, connected: boolean){update(s);live=connected;paint();}};
}
