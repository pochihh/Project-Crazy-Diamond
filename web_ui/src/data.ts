import {csvPreview} from './csvPreview';
type Signal={name:string;unit:string;group:string;numeric:boolean};
type Preview={timestamp_us:number;sequence:number;latest:number[];minimum:number[];maximum:number[]};
import {displayValue} from './instruments';
export type DataState={csv_preview_id:number;control_rate_hz:number;recording:boolean;recording_file:string;recorded_signals:string[];program_name:string;program_id:number;program_rows:number;runner_state:number;runner_row:number;runner_cycles:number;runner_completion:string;runner_error:string;runner_axis_mask:number;robot?:{version:number;id:string;name:string;panel_url:string}|null};
const el=<T extends HTMLElement=HTMLElement>(id:string)=>document.getElementById(id) as T;
export async function dataRequest(action:string,body:object){const r=await fetch(`/api/data/${action}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),signal:AbortSignal.timeout(35000)});const d=await r.json();if(!r.ok)throw Error(d.detail||'Data request failed');return d;}
export function dataTools(){
    let plotStyle = {measured:'#355f42',text:'#555e56',font:'11px monospace'};
    let catalog:Signal[]=[],busy=false,visible=false,ws:WebSocket|undefined,lastSequence=-1,lastTime=0,lastRobot='',recordKey='';
    const selected=new Set(['axis0_reference_mm','axis0_position_mm','axis0_output_duty','axis1_reference_mm','axis1_position_mm','axis1_output_duty','do0']);
    const checks=new Map<string,HTMLInputElement>();
    type Point={t:number;v:number;lo:number;hi:number};
    const plots=new Map<number,{canvas:HTMLCanvasElement;points:Point[];value:HTMLElement}>();
    const message=(text:string)=>{el('data-message').textContent=text;el('runner-message').textContent=text;};
    async function action(name:string,body:object){busy=true;try{const result=await dataRequest(name,body);message(result.warnings?.join('\n')||result.message||'Applied');return result;}catch(e){message(String(e));return undefined;}finally{busy=false;}}
    function rebuild(){
        updateGroupChecks();
        el('data-plots').replaceChildren();plots.clear();
        catalog.forEach((field,index)=>{if(!selected.has(field.name))return;const card=document.createElement('article');card.className='panel data-plot';const title=document.createElement('h3');title.textContent=`${field.name} / ${field.unit}`;card.append(title);const value=document.createElement('span');value.className='mono fine';card.append(value);if(field.numeric){const canvas=document.createElement('canvas');canvas.setAttribute('aria-label',field.name+' live preview');card.append(canvas);plots.set(index,{canvas,points:[],value});}else value.textContent='Text field included in CSV';el('data-plots').append(card);});
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
            const defaults=title.startsWith('Axis ') ? names.filter(n=>/_(target_mm|reference_mm|position_mm|requested_duty|output_duty)$/.test(n)) : names;
            groupChecks.push({input:all,names:defaults});
            all.title=title.startsWith('Axis ') ? 'Toggle target, reference, position and duty' : 'Toggle group';
            all.onchange=()=>{
                for(const name of defaults){if(all.checked)selected.add(name);else selected.delete(name);checks.get(name)!.checked=all.checked;}
                rebuild();
            };
            for(const field of fields) {
                const label=document.createElement('label'),check=document.createElement('input');
                check.type='checkbox';check.checked=selected.has(field.name);
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
    function preview(frame:Preview){if(frame.sequence===lastSequence)return;lastSequence=frame.sequence;if(frame.timestamp_us<=lastTime)plots.forEach(p=>p.points=[]);lastTime=frame.timestamp_us;for(const [index,plot] of plots){const v=frame.latest[index];if(!Number.isFinite(v))continue;plot.points.push({t:frame.timestamp_us,v,lo:frame.minimum[index],hi:frame.maximum[index]});while(plot.points.length>600||plot.points[0]?.t<lastTime-10e6)plot.points.shift();plot.value.textContent=displayValue(v);}}
    function connect(){if(!visible||ws)return;const socket=new WebSocket(`${location.protocol==='https:'?'wss':'ws'}://${location.host}/ws/data`);ws=socket;socket.onmessage=e=>{const data=JSON.parse(e.data);for(const frame of data.frames||[])preview(frame);};socket.onclose=()=>{if(ws===socket)ws=undefined;if(visible)setTimeout(connect,1000);};}
    setInterval(()=>{if(!visible||document.hidden)return;for(const {canvas,points} of plots.values()){const rect=canvas.getBoundingClientRect();if(!rect.width||rect.bottom<0||rect.top>innerHeight||!points.length)continue;const ratio=devicePixelRatio,w=rect.width,h=120;if(canvas.width!==Math.round(w*ratio))canvas.width=Math.round(w*ratio);if(canvas.height!==h*ratio)canvas.height=h*ratio;const ctx=canvas.getContext('2d')!;ctx.setTransform(ratio,0,0,ratio,0,0);ctx.clearRect(0,0,w,h);let lo=Math.min(...points.map(p=>p.lo)),hi=Math.max(...points.map(p=>p.hi));if(hi-lo<1e-9){lo-=.5;hi+=.5;}const y=(v:number)=>h-12-(v-lo)/(hi-lo)*(h-24),x=(t:number)=>w*(1+(t-lastTime)/1e7);ctx.strokeStyle=plotStyle.measured;ctx.lineWidth=1;ctx.beginPath();let previous:Point|undefined;for(const p of points){if(!previous||p.t-previous.t>100000)ctx.moveTo(x(p.t),y(p.v));else ctx.lineTo(x(p.t),y(p.v));previous=p;}ctx.stroke();ctx.globalAlpha=.5;ctx.beginPath();for(const p of points){ctx.moveTo(x(p.t),y(p.lo));ctx.lineTo(x(p.t),y(p.hi));}ctx.stroke();ctx.globalAlpha=1;ctx.fillStyle=plotStyle.text;ctx.font=plotStyle.font;ctx.fillText(displayValue(hi,4),3,10);ctx.fillText(displayValue(lo,4),3,h-1);}},50);
    const showCsv=csvPreview();
    const frame=el<HTMLIFrameElement>('robot-frame');
    window.addEventListener('message',event=>{
        if (!lastRobot || event.source!==frame.contentWindow || event.origin!==new URL(lastRobot).origin) return;
        const height=event.data?.height;
        if(event.data?.type==='pcd.panel.size' && Number.isFinite(height) && height>0 && height<=16384)
            frame.style.height=Math.ceil(height)+'px';
    });
    return {
        setPlotStyle(style:typeof plotStyle){plotStyle=style;},
        tab(name:string){visible=name==='data';if(visible){connect();if(!catalog.length)void signals().catch(e=>message(String(e)));}else{const old=ws;ws=undefined;old?.close();}if(visible)void files().catch(e=>message(String(e)));},
        render(s:DataState,live:boolean){const owned=s.runner_state===1||s.runner_state===2;
            if(visible)void showCsv(s.program_id,s.program_name,s.csv_preview_id,s.control_rate_hz);
            if(!s.recording) recordKey='';
            if(s.recording&&recordKey!==s.recording_file&&catalog.length){selected.clear();for(const name of s.recorded_signals)if(checks.has(name))selected.add(name);checks.forEach((c,n)=>c.checked=selected.has(n));recordKey=s.recording_file;rebuild();}
            groupChecks.forEach(g=>g.input.disabled=s.recording);
            checks.forEach(c=>c.disabled=s.recording);for(const id of ['signals-all','signals-none'])el<HTMLButtonElement>(id).disabled=s.recording;
            el<HTMLButtonElement>('record-start').disabled=busy||!live||s.recording;
            el<HTMLButtonElement>('runner-start').disabled=busy||!live||owned||!s.program_id;
            for(const id of ['runner-load','runner-upload','runner-completion','runner-enable','runner-record']) (el(id) as HTMLInputElement).disabled=busy||owned;
            el<HTMLButtonElement>('runner-stop').disabled=!live;
            el('runner-state').textContent=['IDLE','RUNNING','HOLDING FINAL VALUES','COMPLETE / STOPPED','INTERRUPTED'][s.runner_state]||'—';
            el('runner-progress').textContent=`${s.program_name||'No program loaded'} · ${s.program_rows||0} rows · row ${s.program_rows?s.runner_row+1:0} · ${s.runner_cycles||0} completed cycles`;
            el('runner-active-options').textContent=owned?`Active completion: ${s.runner_completion} · recording ${s.recording?'ON':'OFF'}`:'';
            if(owned){el<HTMLSelectElement>('runner-completion').value=s.runner_completion;el<HTMLInputElement>('runner-record').checked=s.recording;}
            el('runner-error').textContent=s.runner_error||'';
            if(owned)for(let a=0;a<2;a++)if(s.runner_axis_mask&(1<<a))for(const id of ['slider','target','apply','mode','zero'])(el(`${id}-${a}`) as HTMLInputElement).disabled=true;
            const robot=s.robot;const slot=el('robot-slot');slot.hidden=!robot||robot.version!==1;
            if(robot&&robot.version===1){try{const url=new URL(robot.panel_url);if(!['http:','https:'].includes(url.protocol))throw Error();if(lastRobot!==url.href){frame.style.removeProperty('height');el<HTMLIFrameElement>('robot-frame').src=url.href;el<HTMLIFrameElement>('robot-frame').title=robot.name;lastRobot=url.href;}}catch{slot.hidden=true;}}else if(lastRobot){el<HTMLIFrameElement>('robot-frame').src='about:blank';lastRobot='';}
        }
    };
}
