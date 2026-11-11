export type ControllerCommand = {controller_type?: string; profile?: string; parameters?: Record<string,number>; controller_revision?: number};
export async function command(operation: string, axis = 0, value = 0, enabled = false, extra: ControllerCommand = {}) {
  const controller=new AbortController();
  const timeout=setTimeout(()=>controller.abort(),4000);
  try {
    const response=await fetch('/api/command', {
      method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({operation,axis,value,enabled,...extra}),signal:controller.signal,
    });
    const data=await response.json();
    if(!response.ok) throw new Error(typeof data.detail==='string'?data.detail:'Command rejected');
    return data;
  } catch(error) {
    if(controller.signal.aborted) throw new Error('Command timed out. Check the live target before retrying.');
    throw error;
  } finally { clearTimeout(timeout); }
}
