import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import ts from 'typescript';

async function source(name) {
  const code=await readFile(new URL(`../src/${name}.ts`,import.meta.url),'utf8');
  const {outputText}=ts.transpileModule(code,{compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.ESNext}});
  return import(`data:text/javascript;base64,${Buffer.from(outputText).toString('base64')}`);
}
const {PositionPlot}=await source('plot');
const {command}=await source('command');
const {displayValue}=await source('data');

test('near-zero display residues are zero without hiding small measurable values',()=>{
  assert.equal(displayValue(5e-23),'0');
  assert.equal(displayValue(-5e-23),'0');
  assert.equal(displayValue(-0),'0');
  assert.equal(Number(displayValue(.000001)),.000001);
});

const referenceCode=await readFile(new URL('../../robots/phantom_robot/phantom_robot/static/reference.js',import.meta.url),'utf8');
const {numberText,referenceNote}=await import(`data:text/javascript;base64,${Buffer.from(referenceCode).toString('base64')}`);
test('Phantom reports the 20 mm / 1 s sine exceeding a 100 mm/s limiter',()=>{
  const controllers=[{slew_enabled:true,slew_rate_mm_s:100},{slew_enabled:true,slew_rate_mm_s:100}];
  const note=referenceNote(controllers,'sine',1,[20,20]);
  assert.ok(note.limited);
  assert.match(note.text,/125.7 mm\/s/);
  assert.match(note.text,/Passthrough/);
  controllers.forEach(c=>c.slew_enabled=false);
  assert.equal(referenceNote(controllers,'sine',1,[20,20]).limited,false);
  assert.equal(referenceNote(controllers,'csv',1,[20,20]).limited,false);
  assert.equal(numberText(5e-23),'0');
  assert.equal(numberText(-0),'0');
  assert.equal(numberText(.000001),'0.000001');
});

function fakeCanvas() {
  const paths=[];
  const ctx={
    setTransform(){},clearRect(){paths.length=0},setLineDash(){},stroke(){},fillText(){},
    beginPath(){paths.push([])},moveTo(x,y){paths.at(-1).push(['M',x,y])},lineTo(x,y){paths.at(-1).push(['L',x,y])},
  };
  let width=300,height=150,resizes=0,reads=0;
  const canvas={
    get clientWidth(){reads++;return 500},get clientHeight(){reads++;return 120},
    get width(){return width},set width(v){width=v;resizes++},
    get height(){return height},set height(v){height=v;resizes++},
    getContext(){return ctx},
  };
  return {canvas,paths,get resizes(){return resizes},get reads(){return reads}};
}

test('steady plotting does not reallocate canvases or read layout',()=>{
  const f=fakeCanvas(),plot=new PositionPlot(f.canvas);
  plot.resize(2);
  const reads=f.reads;
  for(let i=0;i<1000;i++){plot.add(i*20,1,0.5);plot.draw(i*20)}
  assert.equal(f.resizes,2);
  assert.equal(f.reads,reads);
  assert.ok(f.paths.at(-1).length<=600);
  plot.resize(2);assert.equal(f.resizes,2);
  plot.resize(1);assert.equal(f.resizes,4);
});

test('horizontal spacing follows frame time and missing feedback leaves a gap',()=>{
  const f=fakeCanvas(),plot=new PositionPlot(f.canvas);plot.resize(1);
  plot.add(1000,1,1);plot.add(1020,1,1);plot.add(1100,1,1);plot.draw(1100);
  assert.deepEqual(f.paths.at(-1).map(p=>Math.round(p[1])),[495,496,500]);
  plot.add(1500,1,1);plot.draw(1500);
  assert.equal(f.paths.at(-1).at(-1)[0],'M');
  plot.clear();plot.draw(1500);assert.equal(f.paths.at(-1).length,0);
});

test('command errors and stalled connections do not leave the sender waiting forever',async()=>{
  const original=globalThis.fetch;
  try {
    globalThis.fetch=async(_url,options)=>{
      assert.deepEqual(JSON.parse(options.body),{operation:'target',axis:1,value:2,enabled:false});
      return new Response(JSON.stringify({detail:'rejected'}),{status:409});
    };
    await assert.rejects(command('target',1,2),/rejected/);
    globalThis.fetch=(_url,{signal})=>new Promise((_,reject)=>{
      signal.addEventListener('abort',()=>reject(new DOMException('aborted','AbortError')));
    });
    await assert.rejects(command('target'),/Command timed out/);
    globalThis.fetch=async()=>new Response(JSON.stringify({command_id:5}));
    assert.equal((await command('target')).command_id,5);
  } finally { globalThis.fetch=original; }
});

const {defaults,readSettings}=await source('settings');
test('controller parameters travel together in one command',async()=>{
  const original=globalThis.fetch;
  try {
    globalThis.fetch=async(_url,options)=>{
      const request=JSON.parse(options.body);
      assert.equal(request.operation,'controller_apply');
      assert.equal(request.controller_type,'pid');
      assert.deepEqual(request.parameters,{kp:.0005,ki:.001,kd:0,d_filter_hz:50});
      return new Response(JSON.stringify({command_id:6}));
    };
    await command('controller_apply',1,0,false,{controller_type:'pid',parameters:{kp:.0005,ki:.001,kd:0,d_filter_hz:50}});
  } finally {globalThis.fetch=original;}
});
test('browser settings accept only supported values and recover from stale storage',()=>{
  for(const raw of [null,'null','bad json','{"theme":"blue","plotFps":0,"streamHz":999,"statusSeconds":0,"showPerformance":"true"}'])
    assert.deepEqual(readSettings(raw),defaults);
  const chosen={theme:'dark',plotFps:30,streamHz:10,statusSeconds:5,showPerformance:true};
  assert.deepEqual(readSettings(JSON.stringify(chosen)),chosen);
});

test('PWM plot keeps measured position and omits the unrelated position target',()=>{
  const f=fakeCanvas(),plot=new PositionPlot(f.canvas);plot.resize(1);
  plot.add(1000,50,1);plot.draw(1000);const positionPaths=f.paths.length;
  plot.showTarget=false;plot.draw(1000);
  assert.equal(f.paths.length,positionPaths-1);
  assert.equal(f.paths.at(-1).length,1);
});
