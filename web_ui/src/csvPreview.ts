import {colorCheck, SignalPlot, type PlotFrame, type Trace} from './signalPlot';
type Preview = {rows:number; rows_per_bin:number; channels:string[]; minimum:number[]; maximum:number[]};

export function csvPreview(plot:SignalPlot) {
    const panel = document.getElementById('program-preview')!;
    const controls = document.getElementById('program-channels')!;
    const caption = document.getElementById('program-caption')!;
    let key = '', request: AbortController | undefined;
    return async (id:number, name:string, available:number, rate:number) => {
        const next = [id,name,available,rate].join('/');
        if (next===key) return;
        key=next; request?.abort();
        controls.replaceChildren(); plot.select([]);plot.data([],{start:0,end:1});
        panel.hidden = !id || (id!==available && !name.endsWith('.csv'));
        if (panel.hidden) return;
        if (available!==id) { caption.textContent='Preview unavailable for this load.'; return; }
        request=new AbortController();
        caption.textContent='Loading preview…';
        try {
            const response=await fetch('/api/data/csv_preview?program_id='+id,{signal:request.signal});
            if (!response.ok) throw Error('Preview unavailable for this load.');
            const data:Preview=await response.json();
            if (key!==next) return;
            const span=data.rows/rate;
            caption.textContent=`${name} · ${data.rows.toLocaleString()} ticks · ${span.toFixed(3)} s · min/max per ${data.rows_per_bin} tick(s)`;
            const inputs: HTMLInputElement[]=[];
            const traces: Trace[]=data.channels.map((name,index)=>({name,index,unit:name.endsWith('_mm')?'mm':name.endsWith('_deg')?'deg':name.endsWith('_duty')?'duty':name.startsWith('do')?'bool':'code'}));
            const update=()=>{plot.select(traces.filter((_,i)=>inputs[i].checked));plot.draw();};
            for (const trace of traces) {
                const label=document.createElement('label'), check=document.createElement('input');
                check.type='checkbox'; check.checked=true; check.onchange=update;
                colorCheck(check,trace);inputs.push(check);
                label.append(check,`${trace.name} (${trace.unit})`);controls.append(label);
            }
            const frames:PlotFrame[]=[];
            const n=traces.length;
            for(let row=0,offset=0;row<data.rows;row+=data.rows_per_bin,offset+=n)
                frames.push({time:row/rate,end:Math.min(row+data.rows_per_bin,data.rows)/rate,
                    minimum:data.minimum.slice(offset,offset+n),maximum:data.maximum.slice(offset,offset+n)});
            plot.data(frames,{start:0,end:span});update();
        } catch (error) {
            if (next===key) {
                caption.textContent=String(error);
                const retry=document.createElement('button');retry.textContent='Retry preview';
                retry.onclick=()=>{key='';};controls.append(retry);
            }
        }
    };
}
