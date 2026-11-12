import asyncio
import os
from pathlib import Path
import socket
import threading
import time
import rclpy
from fastapi import FastAPI, HTTPException
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
from rosidl_runtime_py.convert import message_to_ordereddict
from interfaces.msg import RobotPanel
from phantom_interfaces.srv import Command as PhantomCommand
from interfaces.srv import RunnerCommand, LoadCsv, LoadProgram, ListPrograms
from data_tools.ros_client import Operations
from phantom_robot.waveform import sine, DEFAULT_SIGNALS


class Phantom(Operations):
    def __init__(self):
        super().__init__('phantom_robot')
        self.busy=False
        self.configuration={}
        self.last_error=''
        self.port=int(os.environ.get('PHANTOM_PORT','8001'))
        self.url=os.environ.get('PHANTOM_PANEL_URL',f'http://{socket.gethostname()}.local:{self.port}/')
        self.loader=self.create_client(LoadProgram,'/data/program',callback_group=self.group)
        self.csv=self.create_client(LoadCsv,'/data/load_csv',callback_group=self.group)
        self.runner=self.create_client(RunnerCommand,'/data/runner',callback_group=self.group)
        self.files=self.create_client(ListPrograms,'/data/programs',callback_group=self.group)
        self.service=self.create_service(PhantomCommand,'/phantom/command',self.command,callback_group=self.group)
        self.self_client=self.create_client(PhantomCommand,'/phantom/command',callback_group=self.group)
        self.manifest=self.create_publisher(RobotPanel,'/robot/panel',1)
        self.create_timer(.5,lambda:self.manifest.publish(RobotPanel(version=1,id='heart_phantom',name='Heart Phantom',panel_url=self.url)),callback_group=self.group)

    async def command(self, req, out):
        # Stop is allowed while a long load is pending; generation prevents that Start from continuing.
        if req.operation=='stop':
            self.generation+=1
            try:
                await self.request(self.runner,RunnerCommand.Request(operation='stop'))
                out.accepted=True
            except Exception as error:out.message=str(error)
            return out
        if req.operation!='start' or self.busy:
            out.message='Use start/stop; another start may already be pending'
            return out
        self.busy=True;generation=self.generation
        try:
            if self.state is None or not self.state.connected or self.state.fault:
                raise ValueError('Bridge must be connected and fault-free')
            if self.state.runner_state in (1,2) or self.state.recording:
                raise ValueError('Stop the active run/recording before starting a phantom experiment')
            completion=req.completion or ('loop' if req.source=='sine' else 'hold')
            if completion not in ('hold','stop','loop'):raise ValueError('Choose hold, stop or loop')
            config=dict(message_to_ordereddict(req));config.pop('csv',None)
            if req.source=='sine':
                values,period,pulse=sine(req.period_s,req.amplitude_mm,req.offset_mm,req.phase_deg,req.pulse_width_ms)
                await self.request(self.loader,LoadProgram.Request(name='phantom-sine',channels=[0,1,12],values=values))
                config.update(actual_period_s=period,actual_pulse_width_ms=pulse)
            elif req.source=='csv':
                loaded=await self.request(self.csv,LoadCsv.Request(name=req.filename,csv=req.csv,overwrite=req.overwrite))
                if set(loaded.channels)!={0,1,12}:raise ValueError("Phantom CSV requires exactly axis0_reference_mm, axis1_reference_mm and do0")
            else:raise ValueError('Choose sine or csv')
            if generation!=self.generation:raise ValueError('Start cancelled by Stop')
            # This request starts the recorder and first sequence row at one worker boundary.
            await self.request(self.runner,RunnerCommand.Request(operation='start',completion=completion,
                               auto_enable=True,record=True,signals=list(req.signals) or DEFAULT_SIGNALS))
            if generation!=self.generation:
                await self.request(self.runner,RunnerCommand.Request(operation='stop'))
                raise ValueError('Start cancelled by Stop')
            self.configuration=config;self.last_error='';out.accepted=True;out.message='Experiment started'
        except Exception as error:
            self.last_error=str(error);out.message=self.last_error
        finally:self.busy=False
        return out

    generation=0


class Request(BaseModel):
    operation:str='start'
    source:str='sine'
    period_s:float=2
    amplitude_mm:list[float]=Field(default_factory=lambda:[1,1])
    offset_mm:list[float]=Field(default_factory=lambda:[0,0])
    phase_deg:float=0
    pulse_width_ms:float=5
    completion:str='loop'
    filename:str=''
    csv:str=''
    overwrite:bool=False
    signals:list[str]=Field(default_factory=list)


async def response(future):
    deadline=time.monotonic()+30
    while not future.done():
        if time.monotonic()>deadline:raise HTTPException(504,'Operation timed out; inspect live state')
        await asyncio.sleep(.01)
    return dict(message_to_ordereddict(future.result()))


def app_for(node):
    app=FastAPI()
    @app.get('/state')
    async def state():
        return {'configuration':node.configuration,'busy':node.busy,'error':node.last_error,
                'bridge':dict(message_to_ordereddict(node.state)) if node.state else {}}
    @app.get('/files')
    async def files():
        if not node.files.service_is_ready():raise HTTPException(503,'CSV library unavailable')
        return await response(node.files.call_async(ListPrograms.Request()))
    @app.post('/command')
    async def command(body:Request):
        if len(body.amplitude_mm)!=2 or len(body.offset_mm)!=2:raise HTTPException(422,'Exactly two axis values required')
        result=await response(node.self_client.call_async(PhantomCommand.Request(**body.model_dump())))
        if not result['accepted']:raise HTTPException(409,result['message'])
        return result
    app.mount('/',StaticFiles(directory=Path(__file__).parent/'static',html=True))
    return app


def main():
    import uvicorn
    rclpy.init();node=Phantom()
    thread=threading.Thread(target=rclpy.spin,args=(node,),daemon=True);thread.start()
    try:uvicorn.run(app_for(node),host='0.0.0.0',port=node.port,access_log=False,log_level='warning')
    finally:
        rclpy.shutdown();thread.join(timeout=3);node.destroy_node()
