import {csvPreview} from './csvPreview';
type Signal={name:string;unit:string;group:string;numeric:boolean};
type Preview={timestamp_us:number;sequence:number;latest:number[];minimum:number[];maximum:number[]};
import {SignalPlot, colorCheck, type PlotFrame, type Trace} from './signalPlot';
export type DataState={csv_preview_id:number;control_rate_hz:number;recording:boolean;recording_file:string;recorded_signals:string[];program_name:string;program_id:number;program_rows:number;runner_state:number;runner_row:number;runner_cycles:number;runner_completion:string;runner_error:string;runner_axis_mask:number;robot?:{version:number;id:string;name:string;panel_url:string}|null};
const el=<T extends HTMLElement=HTMLElement>(id:string)=>document.getElementById(id) as T;
export async function dataRequest(action:string,body:object){const r=await fetch(`/api/data/${action}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),signal:AbortSignal.timeout(35000)});const d=await r.json();if(!r.ok)throw Error(d.detail||'Data request failed');return d;}
export function dataTools(){
    const loggerPlot=new SignalPlot(el('logger-plot'),'Selected logger signals over the last 10 seconds');
    const runnerPlot=new SignalPlot(el('program-plot'),'Selected CSV commands over program time');
    const history:PlotFrame[]=[];
    let recording=false;
    let catalog:Signal[]=[],busy=false,visible=false,ws:WebSocket|undefined,lastSequence=-1,lastTime=0,lastRobot='',recordKey='';
    const selected=new Set<string>();
    const checks=new Map<string,HTMLInputElement>();
    const message=(text:string)=>{el('data-message').textContent=text;el('runner-message').textContent=text;};
    async function action(name:string,body:object){busy=true;try{const result=await dataRequest(name,body);message(result.warnings?.join('\n')||result.message||'Applied');return result;}catch(e){message(String(e));return undefined;}finally{busy=false;}}
    function rebuild(){
        updateGroupChecks();
        const legend=el('logger-channels');
        legend.replaceChildren();
        const traces:Trace[]=[];
        catalog.forEach((field,index)=>{
            if(!selected.has(field.name) || !field.numeric) return;
            const trace={...field,index};
            traces.push(trace);
            const label=document.createElement('label'),check=document.createElement('input');
            check.type='checkbox';check.checked=true;check.disabled=recording;
            colorCheck(check,trace);
            check.onchange=()=>{selected.delete(field.name);checks.get(field.name)!.checked=false;rebuild();};
            label.append(check,`${field.name} (${field.unit})`);legend.append(label);
        });
        loggerPlot.select(traces);
        loggerPlot.draw();
        el('signal-count').textContent=`${selected.size} selected · timestamp and sample always recorded`;
    }
    const groupChecks: {input:HTMLInputElement; names:string[]}[]=[];
    function updateGroupChecks() {
        for (const {input,names} of groupChecks) {
            const count=names.filter(n=>selected.has(n)).length;
            input.checked=count===names.length;
            input.indeterminate=count>0 && count<names.length;
        }
    }
    function groupOf(field:Signal) {
        if (field.group!=='DSP') return field.group;
        if (/error|invalid_frames|rejected|timeouts|fault/.test(field.name)) return 'DSP / Errors';
        if (/encoder|dsp_reference|poll/.test(field.name)) return 'DSP / Encoders';
        return 'DSP / System';
    }
    async function signals() {
        const r=await fetch('/api/data/signals');const d=await r.json();
        if(!r.ok)throw Error(d.detail);
        if(!catalog.length)for(const field of d.signals as Signal[]) {
            if(/^(do0|axis[01]_((reference|position)_(mm|deg)|output_duty))$/.test(field.name))selected.add(field.name);
        }
        catalog=d.signals; // Preview vectors use the complete catalog's indices.
        const list=el('signal-list'); list.replaceChildren();checks.clear();groupChecks.length=0;
        const groups=new Map<string,Signal[]>();
        for(const field of catalog) {
            if (/^axis\d+_(kp|ki|kd|d_filter_hz|slew_.*|controller_.*|duty_limit)$/.test(field.name)) continue;
            const title=groupOf(field), fields=groups.get(title)??[];
            fields.push(field);groups.set(title,fields);
        }
        const groupNames = [...groups.keys()].sort((a,b) => Number(b.startsWith('Axis ')) - Number(a.startsWith('Axis ')) || a.localeCompare(b));
        for(const title of groupNames) {
            const fields=groups.get(title)!;
            const section=document.createElement('fieldset'),legend=document.createElement('legend');
            const label=document.createElement('label'), all=document.createElement('input');
            all.type='checkbox';label.append(all,title);legend.append(label);section.append(legend);
            const names=fields.map(f=>f.name);
            const defaults=title.startsWith('Axis ') ? names.filter(n=>/_((target|reference|position)_(mm|deg)|requested_duty|output_duty)$/.test(n)) : names;
            groupChecks.push({input:all,names:defaults});
            all.title=title.startsWith('Axis ') ? 'Toggle target, reference, position and duty' : 'Toggle group';
            all.onchange=()=>{
                for(const name of defaults){if(all.checked)selected.add(name);else selected.delete(name);checks.get(name)!.checked=all.checked;}
                rebuild();
            };
            for(const field of fields) {
                const label=document.createElement('label'),check=document.createElement('input');
                check.type='checkbox';check.checked=selected.has(field.name);
                if(field.numeric) colorCheck(check,field);
                label.title=field.name+' ('+field.unit+')';
                check.onchange=()=>{if(check.checked)selected.add(field.name);else selected.delete(field.name);rebuild();};
                label.append(check,field.name.replace(/^axis\d+_/,''));section.append(label);checks.set(field.name,check);
            }
            list.append(section);
        }
        rebuild();
    }
    async function files(){const r=await fetch('/api/data/programs');const d=await r.json();if(!r.ok)throw Error(d.detail);const select=el<HTMLSelectElement>('runner-file'),old=select.value;select.replaceChildren(...d.names.map((name:string)=>new Option(name,name)));if(d.names.includes(old))select.value=old;}
    el('signals-all').onclick=()=>{checks.forEach((c,name)=>{selected.add(name);c.checked=true;});rebuild();};
    el('signals-none').onclick=()=>{selected.clear();checks.forEach(c=>c.checked=false);rebuild();};
    el('record-start').onclick=()=>{if(!selected.size){message('Select at least one signal.');return;}void action('logger',{operation:'start',signals:[...selected]});};
    el('record-stop').onclick=()=>void action('logger',{operation:'stop'});
    el('runner-refresh').onclick=()=>void files().catch(e=>message(String(e)));
    el('runner-load').onclick=()=>void action('load_csv',{name:el<HTMLSelectElement>('runner-file').value});
    el('runner-upload').onclick=async()=>{const file=el<HTMLInputElement>('runner-upload-file').files?.[0];if(!file){message('Choose a CSV file.');return;}const result=await action('load_csv',{name:file.name,csv:await file.text(),overwrite:el<HTMLInputElement>('runner-overwrite').checked});if(result){await files();el<HTMLSelectElement>('runner-file').value=file.name;el<HTMLInputElement>('runner-upload-file').value='';}};
    el('runner-start').onclick=()=>{if(el<HTMLInputElement>('runner-record').checked && !selected.size){message('Select at least one logger signal.');return;}void action('runner',{operation:'start',completion:el<HTMLSelectElement>('runner-completion').value,auto_enable:el<HTMLInputElement>('runner-enable').checked,record:el<HTMLInputElement>('runner-record').checked,signals:[...selected]});};
    el('runner-stop').onclick=()=>void action('runner',{operation:'stop'});
    function connect(){
        if(!visible||ws)return;
        const socket=new WebSocket(`${location.protocol==='https:'?'wss':'ws'}://${location.host}/ws/data`);
        ws=socket;
        socket.onmessage=e=>{
            if(ws!==socket) return;
            const frames:Preview[]=JSON.parse(e.data).frames||[];
            if(!frames.length) return;
            const newest=frames[frames.length-1];
            if(newest.sequence<lastSequence || newest.timestamp_us<lastTime) {
                history.length=0;lastSequence=-1;lastTime=0;
            }
            for(const frame of frames){
                if(frame.sequence<=lastSequence) continue;
                if(frame.timestamp_us<lastTime) history.length=0;
                lastSequence=frame.sequence;lastTime=frame.timestamp_us;
                history.push({time:lastTime/1e6,minimum:frame.minimum,maximum:frame.maximum,latest:frame.latest});
            }
            const end=lastTime/1e6,start=end-10;
            while(history.length && (history[0].time<start || history.length>600)) history.shift();
            loggerPlot.data(history,{start,end,relative:true});
        };
        socket.onclose=()=>{if(ws===socket){ws=undefined;if(visible)setTimeout(connect,1000);}};
    }
    setInterval(()=>{if(visible&&!document.hidden){loggerPlot.draw();runnerPlot.draw();}},50);
    const showCsv=csvPreview(runnerPlot);
    const frame=el<HTMLIFrameElement>('robot-frame');
    window.addEventListener('message',event=>{
        if (!lastRobot || event.source!==frame.contentWindow || event.origin!==new URL(lastRobot).origin) return;
        const height=event.data?.height;
        if(event.data?.type==='pcd.panel.size' && Number.isFinite(height) && height>0 && height<=16384)
            frame.style.height=Math.ceil(height)+'px';
    });
    return {
        setPlotStyle(style:{text:string;grid:string;font:string}){
            const theme={...style,dark:document.documentElement.dataset.theme==='dark'};
            loggerPlot.theme(theme);runnerPlot.theme(theme);
        },
        tab(name:string){visible=name==='data';if(visible){connect();if(!catalog.length)void signals().catch(e=>message(String(e)));}else{const old=ws;ws=undefined;old?.close();}if(visible)void files().catch(e=>message(String(e)));},
        render(s:DataState,live:boolean){const owned=s.runner_state===1||s.runner_state===2;
            if(visible)void showCsv(s.program_id,s.program_name,s.csv_preview_id,s.control_rate_hz);
            if(recording!==s.recording){recording=s.recording;el('logger-channels').querySelectorAll('input').forEach(c=>c.disabled=recording);}
            if(!s.recording) recordKey='';
            if(s.recording&&recordKey!==s.recording_file&&catalog.length){selected.clear();for(const name of s.recorded_signals)if(checks.has(name))selected.add(name);checks.forEach((c,n)=>c.checked=selected.has(n));recordKey=s.recording_file;rebuild();}
            groupChecks.forEach(g=>g.input.disabled=s.recording);
            checks.forEach(c=>c.disabled=s.recording);for(const id of ['signals-all','signals-none'])el<HTMLButtonElement>(id).disabled=s.recording;
            el<HTMLButtonElement>('record-start').disabled=busy||!live||s.recording;
            el<HTMLButtonElement>('runner-start').disabled=busy||!live||owned||!s.program_id||s.program_name.startsWith('Step Recorder / axis ');
            for(const id of ['runner-load','runner-upload','runner-completion','runner-enable','runner-record']) (el(id) as HTMLInputElement).disabled=busy||owned;
            el<HTMLButtonElement>('runner-stop').disabled=!live;
            el('runner-state').textContent=['IDLE','RUNNING','HOLDING FINAL VALUES','COMPLETE / STOPPED','INTERRUPTED'][s.runner_state]||'—';
            el('runner-progress').textContent=`${s.program_name||'No program loaded'} · ${s.program_rows||0} rows · row ${s.program_rows?s.runner_row+1:0} · ${s.runner_cycles||0} completed cycles`;
            el('runner-active-options').textContent=owned?`Active completion: ${s.runner_completion} · recording ${s.recording?'ON':'OFF'}`:'';
            if(owned){el<HTMLSelectElement>('runner-completion').value=s.runner_completion;el<HTMLInputElement>('runner-record').checked=s.recording;}
            el('runner-error').textContent=s.runner_error||'';
            const robot=s.robot;const slot=el('robot-slot');slot.hidden=!robot||robot.version!==1;
            if(robot&&robot.version===1){try{const url=new URL(robot.panel_url);if(!['http:','https:'].includes(url.protocol))throw Error();if(lastRobot!==url.href){frame.style.removeProperty('height');el<HTMLIFrameElement>('robot-frame').src=url.href;el<HTMLIFrameElement>('robot-frame').title=robot.name;lastRobot=url.href;}}catch{slot.hidden=true;}}else if(lastRobot){el<HTMLIFrameElement>('robot-frame').src='about:blank';lastRobot='';}
        }
    };
}
