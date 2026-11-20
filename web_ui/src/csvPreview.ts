type Preview = {rows:number; rows_per_bin:number; channels:string[]; minimum:number[]; maximum:number[]};

// One small chart per channel keeps digital codes and physical units on their own scales.
function channelChart(data: Preview, index: number) {
    const name = data.channels[index];
    const card = document.createElement('figure');
    const lows = data.minimum.filter((_, i) => i % data.channels.length === index);
    const highs = data.maximum.filter((_, i) => i % data.channels.length === index);
    const low = Math.min(...lows), high = Math.max(...highs);
    const title = document.createElement('figcaption');
    title.textContent = `${name}  ·  ${low.toPrecision(3)} … ${high.toPrecision(3)}`;
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('viewBox','0 0 512 84');
    svg.setAttribute('preserveAspectRatio','none');
    svg.setAttribute('role','img'); svg.setAttribute('aria-label',name + ' command preview');
    const y = (v:number) => high===low ? 42 : 79 - 74*(v-low)/(high-low);
    const x = (bin:number) => 512*Math.min(bin*data.rows_per_bin,data.rows)/data.rows;
    const path = document.createElementNS(ns,'path');
    const steps = lows.map((v,i) => `${i ? 'L':'M'}${x(i)} ${y(v)} H${x(i+1)}`);
    const ranges = highs.map((v,i) => `M${(x(i)+x(i+1))/2} ${y(lows[i])} V${y(v)}`);
    path.setAttribute('d',steps.concat(ranges).join(' ')); path.setAttribute('vector-effect','non-scaling-stroke');
    svg.append(path); card.append(title,svg);
    return card;
}
export function csvPreview() {
    const panel = document.getElementById('program-preview')!;
    const controls = document.getElementById('program-channels')!;
    const plots = document.getElementById('program-plots')!;
    const caption = document.getElementById('program-caption')!;
    let key = '', request: AbortController | undefined;
    return async (id:number, name:string, available:number, rate:number) => {
        const next = [id,name,available,rate].join('/');
        if (next===key) return;
        key=next; request?.abort();
        controls.replaceChildren(); plots.replaceChildren();
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
            caption.textContent=`${name} · ${data.rows.toLocaleString()} ticks · ${(data.rows/rate).toFixed(3)} s · min/max per ${data.rows_per_bin} tick(s)`;
            data.channels.forEach((channel,i)=>{
                const label=document.createElement('label'), check=document.createElement('input');
                const chart=channelChart(data,i);
                check.type='checkbox'; check.checked=true; check.onchange=()=>chart.hidden=!check.checked;
                label.append(check,channel); controls.append(label); plots.append(chart);
            });
        } catch (error) {
            if (next===key) {
                caption.textContent=String(error);
                const retry=document.createElement('button');retry.textContent='Retry preview';
                retry.onclick=()=>{key='';};controls.append(retry);
            }
        }
    };
}
