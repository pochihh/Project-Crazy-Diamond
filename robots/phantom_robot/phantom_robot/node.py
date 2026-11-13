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
from interfaces.srv import RunnerCommand, LoadCsv, LoadProgram, ListPrograms, Command
from data_tools.ros_client import Operations
from phantom_robot.waveform import sine, approach_zero, DEFAULT_SIGNALS, MAX_APPROACH_S


class Phantom(Operations):
    def __init__(self):
        super().__init__('phantom_robot')
        self.busy=False
        self.configuration={}
        self.last_error=''
        self.phase=''
        self.received_at=0.0
        self.start_sample=0
        self.port=int(os.environ.get('PHANTOM_PORT','8001'))
        self.url=os.environ.get('PHANTOM_PANEL_URL',f'http://{socket.gethostname()}.local:{self.port}/')
        self.loader=self.create_client(LoadProgram,'/data/program',callback_group=self.group)
        self.csv=self.create_client(LoadCsv,'/data/load_csv',callback_group=self.group)
        self.runner=self.create_client(RunnerCommand,'/data/runner',callback_group=self.group)
        self.host=self.create_client(Command,'/bridge/command',callback_group=self.group)
        self.files=self.create_client(ListPrograms,'/data/programs',callback_group=self.group)
        self.service=self.create_service(PhantomCommand,'/phantom/command',self.command,callback_group=self.group)
        self.self_client=self.create_client(PhantomCommand,'/phantom/command',callback_group=self.group)
        self.manifest=self.create_publisher(RobotPanel,'/robot/panel',1)
        self.create_timer(.5,lambda:self.manifest.publish(RobotPanel(version=1,id='heart_phantom',name='Heart Phantom',panel_url=self.url)),callback_group=self.group)

    def update(self, state):
        super().update(state)
        self.received_at=time.monotonic()

    def check_start(self, generation, stop_count):
        if generation!=self.generation:
            raise ValueError('Start cancelled by Stop')
        s=self.state
        if s is None or time.monotonic()-self.received_at>1 or not s.connected or s.fault:
            raise ValueError('Start cancelled: bridge feedback is unavailable or faulted')
        if s.sample_count<self.start_sample or s.stop_count!=stop_count:
            raise ValueError('Start cancelled by Stop or bridge restart')
        return s

    async def applied_state(self, result, generation, stop_count):
        def ready():
            if generation!=self.generation:raise ValueError('Start cancelled by Stop')
            if self.state is None or self.state.last_command_id<result.command_id:return None
            return self.check_start(generation,stop_count)
        return await self.until(ready)

    async def prepare_zero(self, speed, generation, stop_count):
        # Disable only these axes before taking their measured starting positions.
        self.phase='PREPARING ZERO'
        for axis in range(2):
            self.check_start(generation,stop_count)
            result=await self.request(self.host,Command.Request(operation='axis',axis=axis,enabled=False))
            await self.applied_state(result,generation,stop_count)
        values,duration=approach_zero(list(self.state.position_mm[:2]),speed)
        loaded=await self.request(self.loader,LoadProgram.Request(name='phantom-approach-zero',channels=[0,1,12],values=values))
        await self.applied_state(loaded,generation,stop_count)
        result=await self.request(self.runner,RunnerCommand.Request(operation='start',completion='hold',
                                  auto_enable=True,record=False,check_stop_count=True,expected_stop_count=stop_count))
        await self.applied_state(result,generation,stop_count)
        self.phase='MOVING TO ZERO'
        settled_since=None
        def settled():
            nonlocal settled_since
            s=self.check_start(generation,stop_count)
            if s.program_id!=loaded.command_id or s.runner_state not in (1,2):
                raise ValueError('Zero approach interrupted')
            if s.runner_state==2 and all(abs(v)<=0.1 for v in s.position_mm[:2]):
                self.phase='SETTLING AT ZERO'
                if settled_since is None:settled_since=s.timestamp_us
                if s.timestamp_us-settled_since>=300000:return True
            else:settled_since=None
            return None
        try:await self.until(settled,seconds=duration+10)
        except TimeoutError as error:raise ValueError('Zero approach did not settle within 0.1 mm; experiment not started') from error
        self.check_start(generation,stop_count)
        result=await self.request(self.runner,RunnerCommand.Request(operation='stop'))
        stop_count+=1  # Account only for our own Stop; any external Stop cancels Start.
        await self.applied_state(result,generation,stop_count)
        return stop_count

    async def command(self, req, out):
        # Stop is allowed while a long load is pending; generation prevents that Start from continuing.
        if req.operation=='stop':
            self.generation+=1
            self.phase='STOPPING'
            try:
                await self.request(self.runner,RunnerCommand.Request(operation='stop'))
                self.phase='';out.accepted=True
            except Exception as error:self.phase='';self.last_error=str(error);out.message=str(error)
            return out
        if req.operation!='start' or self.busy:
            out.message='Use start/stop; another start may already be pending'
            return out
        self.busy=True;generation=self.generation;may_be_running=False
        try:
            if self.state is None or time.monotonic()-self.received_at>1 or not self.state.connected or self.state.fault:
                raise ValueError('Bridge must be connected and fault-free')
            if self.state.runner_state in (1,2) or self.state.recording:
                raise ValueError('Stop the active run/recording before starting a phantom experiment')
            stop_count=self.state.stop_count
            self.start_sample=self.state.sample_count
            completion=req.completion or ('loop' if req.source=='sine' else 'hold')
            if completion not in ('hold','stop','loop'):raise ValueError('Choose hold, stop or loop')
            config=dict(message_to_ordereddict(req));config.pop('csv',None)
            if req.source=='sine':
                values,period,pulse=sine(req.period_s,req.amplitude_mm,req.offset_mm,req.phase_deg,req.pulse_width_ms)
                approach_zero([0.0,0.0],req.approach_speed_mm_s)  # Validate speed before changing outputs.
                may_be_running=True
                stop_count=await self.prepare_zero(req.approach_speed_mm_s,generation,stop_count)
                self.phase='STARTING SINE'
                result=await self.request(self.loader,LoadProgram.Request(name='phantom-sine',channels=[0,1,12],values=values))
                await self.applied_state(result,generation,stop_count)
                config.update(actual_period_s=period,actual_pulse_width_ms=pulse)
            elif req.source=='csv':
                loaded=await self.request(self.csv,LoadCsv.Request(name=req.filename,csv=req.csv,overwrite=req.overwrite))
                if set(loaded.channels)!={0,1,12}:raise ValueError("Phantom CSV requires exactly axis0_reference_mm, axis1_reference_mm and do0")
            else:raise ValueError('Choose sine or csv')
            self.check_start(generation,stop_count)
            # This request starts the recorder and first sequence row at one worker boundary.
            may_be_running=True
            await self.request(self.runner,RunnerCommand.Request(operation='start',completion=completion,
                               auto_enable=True,record=True,signals=list(req.signals) or DEFAULT_SIGNALS,
                               check_stop_count=True,expected_stop_count=stop_count))
            if generation!=self.generation:
                await self.request(self.runner,RunnerCommand.Request(operation='stop'))
                raise ValueError('Start cancelled by Stop')
            self.configuration=config;self.last_error='';out.accepted=True;out.message='Experiment started'
        except Exception as error:
            self.last_error=str(error);out.message=self.last_error
            if may_be_running:
                try:await self.request(self.runner,RunnerCommand.Request(operation='stop'))
                except Exception as stop_error:self.last_error+=f'; Stop could not be confirmed: {stop_error}';out.message=self.last_error
        finally:self.busy=False;self.phase=''
        return out

    generation=0


class Request(BaseModel):
    operation:str='start'
    source:str='sine'
    period_s:float=2.0
    amplitude_mm:list[float]=Field(default_factory=lambda:[1.0,1.0])
    offset_mm:list[float]=Field(default_factory=lambda:[0.0,0.0])
    phase_deg:float=0.0
    pulse_width_ms:float=5.0
    approach_speed_mm_s:float=5.0
    completion:str='loop'
    filename:str=''
    csv:str=''
    overwrite:bool=False
    signals:list[str]=Field(default_factory=list)


async def response(future, seconds=30):
    deadline=time.monotonic()+seconds
    while not future.done():
        if time.monotonic()>deadline:raise HTTPException(504,'Operation timed out; inspect live state')
        await asyncio.sleep(.01)
    return dict(message_to_ordereddict(future.result()))


def app_for(node):
    app=FastAPI()
    @app.get('/state')
    async def state():
        return {'configuration':node.configuration,'busy':node.busy,'phase':node.phase,'error':node.last_error,
                'bridge':dict(message_to_ordereddict(node.state)) if node.state else {}}
    @app.get('/files')
    async def files():
        if not node.files.service_is_ready():raise HTTPException(503,'CSV library unavailable')
        return await response(node.files.call_async(ListPrograms.Request()))
    @app.post('/command')
    async def command(body:Request):
        if len(body.amplitude_mm)!=2 or len(body.offset_mm)!=2:raise HTTPException(422,'Exactly two axis values required')
        result=await response(node.self_client.call_async(PhantomCommand.Request(**body.dict())),
                              MAX_APPROACH_S+60 if body.operation=='start' else 30)
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
