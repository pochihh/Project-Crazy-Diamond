// Diagnostic-only Vite proxy. Defaults to a 1 s stream stall every 5 s.
// Never used by the production build or control service.
import {fileURLToPath} from "node:url";
const diagnostic = `
const stats={frames:0,over50ms:0,maxFrameMs:0,longTasks:0,maxLongTaskMs:0,messages:0,maxMessageGapMs:0,canvasResizes:0,domUpdates:0,maxDOMGapMs:0,requests:0,maxRequestMs:0};
let lastFrame=performance.now(),lastMessage=0;
function frame(now){const gap=now-lastFrame;lastFrame=now;stats.frames++;stats.maxFrameMs=Math.max(stats.maxFrameMs,gap);if(gap>50)stats.over50ms++;requestAnimationFrame(frame)}
requestAnimationFrame(frame);
new PerformanceObserver(list=>{for(const e of list.getEntries()){stats.longTasks++;stats.maxLongTaskMs=Math.max(stats.maxLongTaskMs,e.duration)}}).observe({type:'longtask',buffered:true});
for(const key of ['width','height']){const d=Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype,key);Object.defineProperty(HTMLCanvasElement.prototype,key,{...d,set(v){stats.canvasResizes++;d.set.call(this,v)}})}
const NativeSocket=WebSocket;window.WebSocket=class extends NativeSocket{constructor(...args){super(...args);this.addEventListener('message',()=>{const now=performance.now();if(lastMessage)stats.maxMessageGapMs=Math.max(stats.maxMessageGapMs,now-lastMessage);lastMessage=now;stats.messages++})}};
const originalFetch=window.fetch;window.fetch=async(...args)=>{const start=performance.now();const r=await originalFetch(...args);stats.requests++;stats.maxRequestMs=Math.max(stats.maxRequestMs,performance.now()-start);return r};
addEventListener('DOMContentLoaded',()=>{let lastDOM=performance.now();new MutationObserver(()=>{const now=performance.now();stats.domUpdates++;stats.maxDOMGapMs=Math.max(stats.maxDOMGapMs,now-lastDOM);lastDOM=now}).observe(document.getElementById('position-0'),{childList:true,subtree:true,characterData:true});const out=document.createElement('pre');out.id='perf-report';out.style='position:fixed;right:4px;bottom:4px;z-index:1000;background:#fff;border:1px solid;padding:6px;font:10px monospace;pointer-events:none';document.body.append(out);setInterval(()=>out.textContent=JSON.stringify(stats,null,2),1000)});
`;
export default {
 root: fileURLToPath(new URL('../web_ui',import.meta.url)),
 plugins:[{name:'temporary-perf',transformIndexHtml:html=>html.replace('</head>','<script>'+diagnostic+'</script></head>')}],
 server:{host:'127.0.0.1',port:8766,strictPort:true,proxy:{'/api':'http://192.168.1.21:8000','/ws':{target:'ws://192.168.1.21:8000',ws:true,configure(proxy){proxy.on('open',socket=>{const delay=Number(process.env.DIAMOND_WS_STALL_MS??1000);if(!delay)return;const timer=setInterval(()=>{socket.pause();setTimeout(()=>socket.resume(),delay)},5000);socket.on('close',()=>clearInterval(timer))})}}}}
};
