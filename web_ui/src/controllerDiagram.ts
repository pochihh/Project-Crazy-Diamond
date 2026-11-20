/// <reference types="vite/client" />
const documents = import.meta.glob<string>('../../ros2_ws/doc/diagrams/*.md', {eager:true, query:'?raw', import:'default'});

export function controllerDiagram(host: HTMLElement) {
    const image=document.createElement('div'), expand=document.createElement('button');
    const dialog=document.createElement('dialog'), large=document.createElement('div'), close=document.createElement('button');
    const signals=document.createElement('p');signals.className='diagram-signals';
    expand.textContent='Enlarge diagram';close.textContent='Close';
    dialog.className='diagram-dialog';dialog.setAttribute('aria-label','Controller signal diagram');
    dialog.append(close,large);document.body.append(dialog);host.append(image,expand,signals);
    expand.onclick=()=>dialog.showModal();close.onclick=()=>dialog.close();
    let previous='', generation=0,signalKey='';
    return async (type:string, axis:number)=>{
        const doc=documents[`../../ros2_ws/doc/diagrams/${type}.md`];
        if(signalKey!==axis+'/'+type){
            signalKey=axis+'/'+type;
            const fields=doc?.match(/^Signals: (.+)$/m)?.[1].match(/`([a-z_]+)`/g)??[];
            signals.textContent=fields.length ? `Logger · axis${axis}_ prefix · `+fields.map(f=>f.slice(1,-1)).join(' · ') : '';
        }
        if (previous===type) return;
        previous=type;
        const version=++generation;
        const source=doc?.match(/```mermaid\s*\n([\s\S]*?)\n```/)?.[1];
        expand.hidden=true;large.replaceChildren();
        image.textContent=source?'Rendering diagram…':'No diagram documented for this controller.';
        if (!source) return;
        try {
            const {default:mermaid}=await import('mermaid');
            mermaid.initialize({startOnLoad:false,securityLevel:'strict',theme:'neutral',flowchart:{htmlLabels:false}});
            const {svg}=await mermaid.render('controller-graph-'+version,source);
            if(version!==generation) return;
            const url='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg);
            const img=document.createElement('img');
            img.alt=type+' controller signal diagram';img.src=url;
            image.replaceChildren(img);large.replaceChildren(img.cloneNode());expand.hidden=false;
        } catch {
            if(version===generation) image.textContent='Controller diagram could not be rendered.';
        }
    };
}
