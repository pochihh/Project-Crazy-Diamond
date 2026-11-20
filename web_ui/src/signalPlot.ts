// Both previews use seconds on X and one linear Y scale per physical unit.
export type Trace = {name: string; unit: string; index: number};
export type PlotFrame = {time: number; end?: number; latest?: number[]; minimum: number[]; maximum: number[]};
type Extent = {low: number; high: number};
type Window = {start: number; end: number; relative?: boolean};
type Style = {text: string; grid: string; font: string; dark: boolean};

export function traceHue(name: string) {
    // Space the common position, reference and duty channels around the wheel.
    const axis = /^axis(\d+)_(position_(?:mm|deg)|reference_(?:mm|deg)|output_duty)$/.exec(name);
    if (axis) {
        const base = axis[2].startsWith('position_') ? 213 : axis[2].startsWith('reference_') ? 8 : 111;
        return (Number(axis[1])*137 + base) % 360;
    }
    let hash = 0;
    for (const c of name) hash = (Math.imul(hash,31) + c.charCodeAt(0)) >>> 0;
    return hash % 360;
}
export function colorCheck(check: HTMLInputElement, trace: Pick<Trace,'name'>) {
    check.style.setProperty('--hue', String(traceHue(trace.name)));
    check.classList.add('trace-check');
}
export function unitScales(traces: Trace[], frames: PlotFrame[]) {
    const scales = new Map<string,Extent>();
    for (const {index,unit} of traces) {
        if (unit === 'bool' || unit === 'duty') {
            scales.set(unit, {low:unit==='bool' ? 0 : -1, high:1});
            continue;
        }
        const range = scales.get(unit) ?? {low:Infinity,high:-Infinity};
        for (const frame of frames) {
            const lo=frame.minimum[index], hi=frame.maximum[index];
            if (!Number.isFinite(lo) || !Number.isFinite(hi)) continue;
            range.low=Math.min(range.low,lo);
            range.high=Math.max(range.high,hi);
        }
        scales.set(unit,range);
    }
    for (const range of scales.values()) {
        if (!Number.isFinite(range.low)) {range.low=0;range.high=1;}
        else if (range.low===range.high) {range.low-=.5;range.high+=.5;}
    }
    return scales;
}
const numberText = (v: number) => String(Number(v.toPrecision(6)));

export class SignalPlot {
    private canvas = document.createElement('canvas');
    private caption = document.createElement('p');
    private style:Style = {text:'#555e56',grid:'#b7b9aa',font:'11px monospace',dark:false};
    private dirty = true;
    private traces:Trace[] = [];
    private frames:PlotFrame[] = [];
    private window:Window = {start:0,end:10};
    constructor(container: HTMLElement, name: string) {
        this.canvas.setAttribute('role','img');
        this.canvas.setAttribute('aria-label', name);
        this.caption.className='scale-key';
        this.caption.id=container.id+'-scales';
        this.canvas.setAttribute('aria-describedby',this.caption.id);
        container.append(this.canvas,this.caption);
        new ResizeObserver(()=>{this.dirty=true;this.draw();}).observe(this.canvas);
    }
    select(traces:Trace[]) {this.traces=traces;this.dirty=true;}
    data(frames:PlotFrame[], window:Window) {this.frames=frames;this.window=window;this.dirty=true;}
    theme(style:Style) {this.style=style;this.dirty=true;}
    draw() {
        if (!this.dirty) return;
        const canvas=this.canvas, rect=canvas.getBoundingClientRect();
        if (!rect.width || rect.bottom<0 || rect.top>innerHeight) return;
        this.dirty=false;
        const w=canvas.clientWidth,h=canvas.clientHeight,ratio=devicePixelRatio;
        if(w<=0 || h<=0) return;
        if(canvas.width!==Math.round(w*ratio)) canvas.width=Math.round(w*ratio);
        if(canvas.height!==Math.round(h*ratio)) canvas.height=Math.round(h*ratio);
        const ctx=canvas.getContext('2d')!;
        ctx.setTransform(ratio,0,0,ratio,0,0);
        ctx.clearRect(0,0,w,h);
        const {frames,traces,style}=this;
        if(!traces.length || !frames.length) {
            this.caption.textContent=traces.length?'Waiting for preview samples.':'Select signals to plot.';
            return;
        }
        const scales=unitScales(traces,frames), axes=[...scales.entries()];
        const key=axes.map(([unit,r])=>unit+'  '+numberText(r.low)+' … '+numberText(r.high)).join('   |   ');
        if(this.caption.textContent!==key) this.caption.textContent=key;
        ctx.font=style.font;
        ctx.fillStyle=style.text;
        const axisWidth=([unit,r]:[string,Extent]) => Math.min(w*.22,8+Math.max(...[unit,numberText(r.low),numberText(r.high)].map(v=>ctx.measureText(v).width)));
        const left=axisWidth(axes[0])+4, right=w-(axes[1]?axisWidth(axes[1])+4:8), top=22,bottom=h-24;
        const x=(t:number)=>left+(t-this.window.start)/(this.window.end-this.window.start)*(right-left);
        for(let i=0;i<=4;i++) {
            const y=top+(bottom-top)*i/4;
            ctx.strokeStyle=style.grid;ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();
            const t=this.window.start+(this.window.end-this.window.start)*i/4;
            ctx.textAlign=i===0?'left':i===4?'right':'center';
            ctx.fillText(numberText(this.window.relative?t-this.window.end:t)+' s',x(t),h-6);
        }
        axes.slice(0,2).forEach(([unit,r],i)=>{
            const offset=i?right+4:left-4;
            ctx.textAlign=i?'left':'right';
            ctx.fillText(unit,offset,12);
            ctx.fillText(numberText(r.high),offset,top+5);
            ctx.fillText(numberText((r.low+r.high)/2),offset,(top+bottom)/2+3);
            ctx.fillText(numberText(r.low),offset,bottom);
        });
        ctx.save();ctx.beginPath();ctx.rect(left-1,top-1,right-left+2,bottom-top+2);ctx.clip();
        for(const trace of traces) {
            const {low,high}=scales.get(trace.unit)!;
            const y=(v:number)=>bottom-(v-low)/(high-low)*(bottom-top);
            const i=trace.index;
            ctx.strokeStyle=`hsl(${traceHue(trace.name)} 62% ${style.dark?70:35}%)`;
            ctx.lineWidth=1.6;
            ctx.beginPath();
            let previous:PlotFrame|undefined;
            for(const frame of frames) {
                const v=frame.latest?.[i], lo=frame.minimum[i], hi=frame.maximum[i], xx=x(frame.time);
                if(!Number.isFinite(lo)||!Number.isFinite(hi)) {previous=undefined;continue;}
                if(frame.end!==undefined) {
                    // CSV bins carry extrema only. Draw both bounds over their actual span.
                    const end=x(frame.end);
                    ctx.moveTo(xx,y(lo));ctx.lineTo(end,y(lo));
                    if(hi!==lo){ctx.moveTo(xx,y(hi));ctx.lineTo(end,y(hi));ctx.moveTo((xx+end)/2,y(lo));ctx.lineTo((xx+end)/2,y(hi));}
                    if(previous && previous.maximum[i]===previous.minimum[i] && lo===hi) {
                        ctx.moveTo(xx,y(previous.maximum[i]));ctx.lineTo(xx,y(lo));
                    }
                } else if(v!==undefined && Number.isFinite(v)) {
                    if(previous?.latest && frame.time-previous.time<=.10) {
                        const last=previous.latest[i];
                        ctx.moveTo(x(previous.time),y(last));
                        if(trace.unit==='bool') ctx.lineTo(xx,y(last));
                        ctx.lineTo(xx,y(v));
                    }
                    ctx.moveTo(xx,y(lo));ctx.lineTo(xx,y(hi)+(lo===hi?.5:0));
                }
                previous=frame;
            }
            ctx.stroke();
        }
        ctx.restore();
    }
}
