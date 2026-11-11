// CCS Scripting: run.sh scripts/load_dsp.js RAM|FLASH
// Stop diamond-bridge first. Connecting with TI's default GEL resets/clears RAM.
const {join}=require('path');
const root=join(__dirname,'..');
const configuration=process.argv[3];
if(!['RAM','FLASH'].includes(configuration))throw Error('Specify RAM or FLASH');
const ds=initScripting({timeout:120000});
try {
    ds.configure(join(root,'dsp/f28379d/targetConfigs/TMS320F28379D.ccxml'));
    const cpu1=ds.openSession(/C28xx_CPU1$/);
    cpu1.target.connect();cpu1.target.halt();cpu1.target.reset('System Reset');
    const cpu2=ds.openSession(/C28xx_CPU2$/);cpu2.target.connect();cpu2.target.halt();
    // Both cores are halted while either flash bank is programmed.
    // CPU1's load/reset releases CPU2 reset, so load CPU2 last.
    for(const [number,cpu] of [[1,cpu1],[2,cpu2]]){
        const file=join(root,`dsp/f28379d/cpu${number}/${configuration}/f28379d_cpu${number}.out`);
        cpu.memory.loadProgram(file);cpu.memory.verifyProgram(file);
        console.log(`CPU${number} ${configuration}: loaded and verified`);
    }
    cpu1.target.run(false);cpu2.target.run(false);sleep(2000);
    for(const name of ['gTickCount','gMaxTickUs','gEncoderValid','endpoint.fault'])
        console.log('CPU1',name,cpu1.expressions.evaluate(name));
    for(const name of ['gPolls','gMaxPollCycles'])console.log('CPU2',name,cpu2.expressions.evaluate(name));
    if(cpu1.expressions.evaluate('gEncoderValid')!==63n || cpu2.expressions.evaluate('gPolls')===0n)
        throw Error('Both encoder cores must be running before hardware acceptance');
    // Disconnecting one target may also disconnect the other core.
    if(cpu2.target.isConnected())cpu2.target.disconnect();
    if(cpu1.target.isConnected())cpu1.target.disconnect();
} finally {ds.shutdown();}
