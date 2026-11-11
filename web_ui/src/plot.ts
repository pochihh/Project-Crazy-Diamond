type Point = { time: number; target: number; position: number };
const windowMs = 10_000;

export class PositionPlot {
  showTarget=true;
  colors={ink:'#272a26',grid:'#d4d3c4',text:'#6f7168'};
  private points: Point[] = [];
  private width = 0;
  private height = 0;
  private ratio = 1;
  private readonly ctx: CanvasRenderingContext2D;

  constructor(private readonly canvas: HTMLCanvasElement) {
    this.ctx = canvas.getContext('2d')!;
  }

  clear() { this.points = []; }

  add(time: number, target: number, position: number) {
    this.points.push({time,target,position});
    while(this.points.length>600 || (this.points.length>1 && this.points[1].time<time-windowMs)) this.points.shift();
  }

  resize(ratio: number) {
    this.width = this.canvas.clientWidth;
    this.height = this.canvas.clientHeight;
    this.ratio = ratio;
    if(!this.width || !this.height) return;
    const width=Math.round(this.width*ratio), height=Math.round(this.height*ratio);
    // Changing either dimension clears/reallocates the canvas, even at the same size.
    if(this.canvas.width!==width) this.canvas.width=width;
    if(this.canvas.height!==height) this.canvas.height=height;
  }

  draw(endTime: number) {
    const w=this.width,h=this.height,ctx=this.ctx;
    if(!w || !h || !Number.isFinite(endTime)) return;
    ctx.setTransform(this.ratio,0,0,this.ratio,0,0);
    ctx.clearRect(0,0,w,h);
    let maximum=0;
    for(const p of this.points) maximum=Math.max(maximum,Math.abs(p.position),this.showTarget?Math.abs(p.target):0);
    const extent=Math.max(2,Math.ceil(maximum*1.12));
    const y=(v:number)=>h/2-v/extent*(h/2-12);
    ctx.strokeStyle=this.colors.grid;ctx.lineWidth=1;ctx.setLineDash([]);
    for(let i=0;i<5;i++){ctx.beginPath();ctx.moveTo(0,12+i*(h-24)/4);ctx.lineTo(w,12+i*(h-24)/4);ctx.stroke();}
    ctx.fillStyle=this.colors.text;ctx.font='10px monospace';ctx.fillText(`${extent.toFixed(1)} mm`,4,10);
    for(const [field,color] of [['target',this.colors.ink],['position','#75916b']] as const) {
      if(field==='target'&&!this.showTarget) continue;
      ctx.strokeStyle=color;ctx.lineWidth=field==='position'?2:1;ctx.setLineDash(field==='target'?[5,4]:[]);ctx.beginPath();
      let previous: Point | undefined;
      for(const p of this.points) {
        const x=w*(1+(p.time-endTime)/windowMs);
        // Leave a visible gap when samples are missing; do not invent smooth feedback.
        if(!previous || p.time-previous.time>250) ctx.moveTo(x,y(p[field])); else ctx.lineTo(x,y(p[field]));
        previous=p;
      }
      ctx.stroke();
    }
  }
}
