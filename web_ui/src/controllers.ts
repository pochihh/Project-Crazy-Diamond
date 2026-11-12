import { command, type ControllerCommand } from './command';
export type Profile = {id: string; type: string; parameter_names: string[]; parameter_values: number[]; slew_enabled: boolean; slew_rate_mm_s: number};
type ControllerType = {type: string; label: string; parameter_names: string[]; parameter_units: string[]; defaults: number[]; minimum: number[]; maximum: number[]};
export type ControllerState = {controllers: Profile[]; controller_revision: number[]; control_rate_hz: number; system_enabled: boolean; axis_enabled: boolean[]; duty_limit: number[]; control_mode: number[]; fault: string};
type Catalog = {types: ControllerType[]; profiles: Profile[]; errors: string[]};

export function controllerPanel() {
    const panel = document.createElement('aside');
    panel.className = 'settings-panel panel'; panel.id = 'controller-panel'; panel.hidden = true;
    panel.setAttribute('aria-label', 'Axis controller settings');
    panel.innerHTML = `<div class="panel-head"><span id="controller-title">AXIS CONTROLLER</span><button id="controller-close" class="icon-close" aria-label="Close controller settings">×</button></div><div class="settings-body"><label>Controller<select id="controller-type"></select></label><p>Change type while disabled. Parameters can be tuned while running. LQR awaits its state model.</p><label>Saved profile<select id="controller-profile"></select></label><div class="controller-actions"><button id="profile-load">LOAD / RELOAD</button><button id="profiles-refresh" class="secondary">REFRESH</button></div><p id="controller-profile-status"></p><div id="controller-parameters" class="controller-parameters"></div><label>Reference processing<select id="reference-type"><option value="passthrough">Passthrough / no shaping</option><option value="slew_limit">Slew limit</option></select></label><label>Maximum reference rate / mm/s<input id="reference-rate" type="number" min="0.000001" step="any" value="20"></label><button id="controller-apply">APPLY PARAMETERS</button><div class="controller-actions"><button id="profile-save">SAVE PROFILE</button></div><label>New profile ID<input id="profile-new" placeholder="bench_pid_v2" maxlength="63" pattern="[a-z0-9_-]+"></label><button id="profile-create">SAVE AS NEW PROFILE</button><hr><label>Axis duty limit / 0–1<input id="controller-limit" type="number" min="0.000001" max="1" step="any"></label><button id="controller-limit-apply">SET DUTY LIMIT</button><p>Duty limit requires this axis disabled and stays in the axis configuration, outside the shared profile.</p><p id="controller-reference"></p><p id="controller-feedback" role="status"></p></div>`;
    document.querySelector('.shell')!.append(panel);
    const el = <T extends HTMLElement = HTMLElement>(id: string) => panel.querySelector<T>(`#${id}`)!;
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
    function paint() {
        if(panel.hidden || !current?.controllers?.[axis]) return;
        const active = current.controllers[axis], enabled = current.system_enabled && current.axis_enabled[axis] && !current.fault;
        if(!dirty && revision !== current.controller_revision[axis] && catalog.types.length) {
            type.value = active.type; fields(active.type,active.parameter_values);
            profile.value = active.id; revision = current.controller_revision[axis];
            el<HTMLSelectElement>("reference-type").value=active.slew_enabled?"slew_limit":"passthrough";
            el<HTMLInputElement>("reference-rate").value=String(active.slew_rate_mm_s);
        }
        const saved = catalog.profiles.find(p => p.id === active.id);
        const differs = !saved || saved.type !== active.type || saved.slew_enabled!==active.slew_enabled || saved.slew_rate_mm_s!==active.slew_rate_mm_s || saved.parameter_names.some((name,i) => active.parameter_values[active.parameter_names.indexOf(name)] !== saved.parameter_values[i]);
        el('controller-profile-status').textContent = dirty ? `Editing ${active.id || 'working copy'} — apply before saving.` :
            `${active.id || 'Working copy'} · ${differs ? 'active parameters differ from saved profile; Save or Reload explicitly' : 'matches saved profile'}`;
        for(const control of Array.from(panel.querySelectorAll<HTMLInputElement | HTMLButtonElement | HTMLSelectElement>('input, button, select')))
            if(control.id !== 'controller-close') control.disabled = busy || !live;
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
            active.slew_enabled ? `Profile reference limit: ${active.slew_rate_mm_s} mm/s. No acceleration limit.` : 'Passthrough: reference reaches the controller unchanged.';
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
        busy = true; paint(); feedback('Applying…');
        try {
            const result = await command(operation,axis,value,false,extra);
            current = result.state;
            if(operation !== 'limit') { dirty = false; revision = -1; }
            if(operation.startsWith('profile_')) await refresh();
            feedback(operation === 'profile_save' || operation === 'profile_create' ? 'Profile saved. Other axes keep their active parameters until reloaded.' : 'Applied.');
        } catch(error) { feedback(String(error)); }
        finally { busy = false; paint(); }
    }
    el('controller-close').onclick = () => { panel.hidden = true; };
    type.onchange = () => { dirty = true; fields(type.value,catalog.types.find(t => t.type === type.value)?.defaults ?? []); paint(); };
    el('controller-apply').onclick = () => {
        const parameters: Record<string,number> = {};
        for(const input of Array.from(panel.querySelectorAll<HTMLInputElement>('#controller-parameters input'))) {
            if(!input.value || !input.checkValidity() || !Number.isFinite(Number(input.value))) { feedback('Enter finite parameters within the displayed bounds.'); return; }
            parameters[input.name] = Number(input.value);
        }
        const rate=Number(el<HTMLInputElement>('reference-rate').value);
        if(!Number.isFinite(rate)||rate<=0){feedback('Reference rate must be positive and finite.');return;}
        void send('controller_apply',{controller_type: type.value,parameters,set_reference:true,slew_enabled:el<HTMLSelectElement>('reference-type').value==='slew_limit',slew_rate_mm_s:rate});
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
    for(let a=0;a<2;a++) document.getElementById(`controller-open-${a}`)!.onclick = () => {
        if(busy) return;
        axis = a; dirty = false; revision = -1; feedback('');
        el<HTMLInputElement>('controller-limit').removeAttribute('data-dirty');
        el('controller-title').textContent = `AXIS ${a} / CONTROLLER`;
        document.getElementById('settings-panel')!.hidden = true;
        const web = document.getElementById('settings-button')!; web.setAttribute('aria-expanded','false'); web.classList.remove('active');
        panel.hidden = false; paint(); void refresh().catch(e => feedback(String(e)));
    };
    return {render(s: ControllerState, connected: boolean){current=s;live=connected;paint();},close(){panel.hidden=true;}};
}
