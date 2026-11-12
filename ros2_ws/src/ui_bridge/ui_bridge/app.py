"""Trusted-LAN API. Browser lifetime never owns motor or recording lifetime."""
from collections import deque
import asyncio
import math
import json
import os
import re
from pathlib import Path
import threading
import time
import uuid

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
import rclpy
from rclpy.node import Node
from rosidl_runtime_py.convert import message_to_ordereddict
from rosidl_runtime_py.utilities import get_message
from rclpy.qos import QoSProfile, ReliabilityPolicy
from interfaces.msg import DataPreview, RobotPanel, BridgeState, CommandResult, PositionCommand, DutyCommand, DigitalCommand, AnalogCommand
from interfaces.srv import LoggerCommand, RunnerCommand, LoadCsv, ListPrograms, SignalCatalog, Command, ControllerCatalog, DspCommand
from ui_bridge.system_status import SystemStatus


class RosClient(Node):
    def __init__(self):
        super().__init__('ui_bridge')
        self.state = {}
        self.received = 0.0
        self.sequence = 0
        self.instance = uuid.uuid4().hex
        self.client = self.create_client(Command, '/bridge/command')
        self.dsp_client = self.create_client(DspCommand, '/dsp/command')
        self.inputs = {
            'target': (self.create_publisher(PositionCommand, '/bridge/target', 16), PositionCommand, 'axis', 'position_mm'),
            'pwm': (self.create_publisher(DutyCommand, '/dsp/duty', 16), DutyCommand, 'axis', 'duty'),
            'digital': (self.create_publisher(DigitalCommand, '/dsp/digital_out', 16), DigitalCommand, 'channel', 'value'),
            'analog': (self.create_publisher(AnalogCommand, '/dsp/analog_out', 16), AnalogCommand, 'channel', 'value'),
        }
        self.catalog_client = self.create_client(ControllerCatalog, '/bridge/controllers')
        self.preview = {}
        self.previews = deque(maxlen=100)
        self.robot = {}
        self.robot_received = 0.0
        self.data_clients = {name:self.create_client(kind,topic) for name,kind,topic in [
            ('logger',LoggerCommand,'/data/logger'),('runner',RunnerCommand,'/data/runner'),
            ('load_csv',LoadCsv,'/data/load_csv'),('programs',ListPrograms,'/data/programs'),('signals',SignalCatalog,'/data/signals')]}
        self.preview_sub = self.create_subscription(DataPreview,'/data/preview',self.update_preview,
            QoSProfile(depth=1,reliability=ReliabilityPolicy.BEST_EFFORT))
        self.robot_sub = self.create_subscription(RobotPanel,'/robot/panel',self.update_robot,1)
        self.results = {}
        self.result_subscription = self.create_subscription(CommandResult, '/bridge/command_result', self.result, 256)
        self.subscription = self.create_subscription(BridgeState, '/bridge/state', self.update, 1)

    def update_preview(self, message):
        if self.preview and message.sequence<=self.preview["sequence"]: self.previews.clear()
        self.preview = dict(message_to_ordereddict(message))
        self.previews.append(self.preview)

    def update_robot(self, message):
        self.robot = dict(message_to_ordereddict(message))
        self.robot_received = time.monotonic()

    def result(self, message):
        self.results[message.command_id] = message
        while len(self.results) > 256:
            del self.results[next(iter(self.results))]

    def update(self, message):
        if message.sample_count < self.state.get('sample_count', 0):
            self.results.clear()
        self.sequence += 1
        self.state = dict(message_to_ordereddict(message), ui_sequence=self.sequence, ui_instance=self.instance)
        self.received = time.monotonic()

    def publish_input(self, operation, channel, value):
        publisher, message_type, channel_field, value_field = self.inputs[operation]
        if operation == 'digital':
            value = bool(value)
        elif operation == 'analog':
            value = int(value)
        publisher.publish(message_type(**{channel_field: channel, value_field: value}))

    def snapshot(self):
        state = self.state.copy()
        age = time.monotonic() - self.received
        state.update(bridge_alive=age < 1.0, state_age_ms=round(age * 1000) if self.received else None)
        state['robot'] = self.robot if time.monotonic()-self.robot_received < 2 else None
        return state


class Request(BaseModel):
    operation: str
    axis: int = Field(default=0, ge=0, le=5)
    value: float = 0.0
    enabled: bool = False
    set_reference: bool = False
    slew_enabled: bool = False
    slew_rate_mm_s: float = 20.0
    controller_type: str = ''
    profile: str = ''
    parameters: dict[str, float] = Field(default_factory=dict)
    controller_revision: int = Field(default=0, ge=0)


class DataRequest(BaseModel):
    operation: str = ''
    name: str = ''
    csv: str = ''
    overwrite: bool = False
    completion: str = 'hold'
    record: bool = False
    auto_enable: bool = True
    signals: list[str] = Field(default_factory=list)


def load_project(root: Path):
    project_id = os.environ.get('DIAMOND_PROJECT', 'heart_phantom')
    if not re.fullmatch(r'[a-z0-9_-]+', project_id):
        raise ValueError('Invalid DIAMOND_PROJECT')
    project = json.loads((root / 'projects' / project_id / 'project.json').read_text())
    if project.get('id') != project_id or len(project.get('axes', [])) != 2:
        raise ValueError('Project manifest must match its directory and define two axes')
    return project


def create_app(ros, recordings: Path, static: Path, project=None):
    app = FastAPI(title='Crazy Diamond')
    command_lock = asyncio.Lock()
    status = SystemStatus(recordings.parent)

    @app.get('/api/project')
    async def project_info():
        return project or {'id': 'heart_phantom', 'name': 'Heart Phantom', 'axes': [{'name': 'Axis 0'}, {'name': 'Axis 1'}]}

    @app.get('/api/state')
    async def state():
        return ros.snapshot()

    @app.get('/api/controllers')
    async def controllers():
        if not ros.catalog_client.service_is_ready():
            raise HTTPException(503, 'Controller catalog is unavailable')
        future = ros.catalog_client.call_async(ControllerCatalog.Request())
        deadline = time.monotonic() + 3
        while not future.done():
            if time.monotonic() > deadline:
                raise HTTPException(504, 'Controller catalog timed out')
            await asyncio.sleep(0.005)
        return dict(message_to_ordereddict(future.result()))

    async def data_service(name, request, applied=False):
        client = ros.data_clients[name]
        if not client.service_is_ready():
            raise HTTPException(503, 'Data service unavailable')
        future = client.call_async(request)
        deadline = time.monotonic()+30
        while not future.done():
            if time.monotonic()>deadline: raise HTTPException(504, 'Data operation timed out; inspect live state')
            await asyncio.sleep(.01)
        result = future.result()
        if hasattr(result,'accepted') and not result.accepted:
            raise HTTPException(409,result.message)
        if applied and result.command_id:
            while result.command_id not in ros.results or ros.state.get('last_command_id',0)<result.command_id:
                if time.monotonic()>deadline: raise HTTPException(504,'Data application timed out; inspect live state')
                await asyncio.sleep(.005)
            ack=ros.results[result.command_id]
            if not ack.applied: raise HTTPException(409,ack.message)
        return dict(message_to_ordereddict(result))

    @app.get('/api/data/signals')
    async def signals(): return await data_service('signals',SignalCatalog.Request())

    @app.get('/api/data/programs')
    async def programs(): return await data_service('programs',ListPrograms.Request())

    @app.post('/api/data/{action}')
    async def data_command(action: str, body: DataRequest):
        if action=='logger': request=LoggerCommand.Request(operation=body.operation,signals=body.signals)
        elif action=='runner': request=RunnerCommand.Request(operation=body.operation,completion=body.completion,record=body.record,auto_enable=body.auto_enable,signals=body.signals)
        elif action=='load_csv': request=LoadCsv.Request(name=body.name,csv=body.csv,overwrite=body.overwrite)
        else: raise HTTPException(404,'Unknown data action')
        return await data_service(action,request,True)

    @app.websocket('/ws/data')
    async def data_preview(socket: WebSocket):
        await socket.accept()
        cursor=-1
        try:
            while True:
                frames=list(ros.previews)
                if frames and frames[-1]['sequence']<cursor: cursor=-1
                frames=[frame for frame in frames if frame['sequence']>cursor]
                await socket.send_json({'frames':frames})
                if frames: cursor=frames[-1]['sequence']
                try:
                    event=await asyncio.wait_for(socket.receive(),timeout=.05)
                    if event['type']=='websocket.disconnect': break
                except asyncio.TimeoutError: pass
        except (WebSocketDisconnect,RuntimeError): pass

    @app.post('/api/command')
    async def command(body: Request):
        if body.axis >= (4 if body.operation == 'digital' else 2 if body.operation == 'analog' else 6):
            raise HTTPException(422, 'Invalid axis or I/O channel')
        if not math.isfinite(body.value):
            raise HTTPException(422, 'Value must be finite')
        if len(body.parameters) > 16 or any(not math.isfinite(v) for v in body.parameters.values()):
            raise HTTPException(422, 'Controller parameters must be finite; at most 16 are supported')
        if not ros.snapshot().get('bridge_alive'):
            raise HTTPException(503, 'Control bridge is unavailable')
        if body.operation in ('target', 'pwm', 'digital', 'analog'):
            # Transport/type checks only. The C++ gate owns mode/range checks.
            if body.operation in ('digital', 'analog') and (body.value != int(body.value) or
                    not 0 <= body.value <= (1 if body.operation == 'digital' else 4095)):
                raise HTTPException(422, 'Invalid output code')
            ros.publish_input(body.operation, body.axis, body.value)
            return {'published': True, 'state': ros.snapshot()}
        client = ros.dsp_client if body.operation in ('dsp_clear', 'dsp_disarm') else ros.client
        if not client.service_is_ready():
            raise HTTPException(503, 'Command service is unavailable')
        async with command_lock:
            if body.operation in ('dsp_clear', 'dsp_disarm'):
                request = DspCommand.Request()
                request.operation = DspCommand.Request.CLEAR_FAULT if body.operation == 'dsp_clear' else DspCommand.Request.DISARM
            else:
                request = Command.Request()
                request.operation, request.axis = body.operation, body.axis
                request.value, request.enabled = body.value, body.enabled
                request.controller_type, request.profile = body.controller_type, body.profile
                request.parameter_names = list(body.parameters)
                request.parameter_values = list(body.parameters.values())
                request.controller_revision = body.controller_revision
                request.set_reference = body.set_reference
                request.slew_enabled, request.slew_rate_mm_s = body.slew_enabled, body.slew_rate_mm_s
            future = client.call_async(request)
            deadline = time.monotonic() + 3
            while not future.done():
                if time.monotonic() > deadline:
                    raise HTTPException(504, 'Bridge command timed out')
                await asyncio.sleep(0.005)
            result = future.result()
            if not result.accepted:
                raise HTTPException(409, result.message)
            while result.command_id not in ros.results or ros.state.get('last_command_id', 0) < result.command_id:
                if time.monotonic() > deadline:
                    raise HTTPException(504, 'Command acknowledgment timed out')
                await asyncio.sleep(0.005)
            applied = ros.results.pop(result.command_id)
            if not applied.applied:
                raise HTTPException(409, applied.message)
            response = {'command_id': result.command_id, 'state': ros.snapshot()}
            if body.operation in ('controller_apply', 'profile_load', 'profile_save', 'profile_create', 'kp', 'ki', 'kd', 'limit'):
                if not applied.has_controller or applied.axis != body.axis:
                    raise HTTPException(502, 'Command acknowledged without controller read-back')
                actual = dict(message_to_ordereddict(applied.controller))
                parameters = dict(zip(actual['parameter_names'], actual['parameter_values']))
                matches = True
                if body.operation == 'controller_apply':
                    matches = actual['type'] == body.controller_type and parameters == body.parameters
                    if body.set_reference:
                        matches = matches and actual['slew_enabled'] == body.slew_enabled and actual['slew_rate_mm_s'] == body.slew_rate_mm_s
                elif body.operation in ('kp', 'ki', 'kd'):
                    matches = parameters.get(body.operation) == body.value
                elif body.operation == 'limit':
                    matches = applied.duty_limit == body.value
                else:
                    matches = actual['id'] == body.profile
                if not matches:
                    raise HTTPException(409, 'Controller read-back differs from the request; inspect the active settings')
                response['controller_readback'] = {
                    'axis': applied.axis, 'controller_revision': applied.controller_revision,
                    'controller': actual, 'duty_limit': applied.duty_limit,
                }
            return response

    @app.websocket('/ws')
    async def websocket(socket: WebSocket):
        try:
            hz = int(socket.query_params.get('hz', '50'))
            if not 10 <= hz <= 50:
                raise ValueError()
        except ValueError:
            await socket.close(code=1008)
            return
        await socket.accept()
        try:
            while True:
                await socket.send_json(ros.snapshot())
                try:
                    event = await asyncio.wait_for(socket.receive(), timeout=1 / hz)
                    if event['type'] == 'websocket.disconnect':
                        break
                except asyncio.TimeoutError:
                    pass
        except (WebSocketDisconnect, RuntimeError):
            pass

    @app.websocket('/ws/topics')
    async def topic_stream(socket: WebSocket):
        await socket.accept()
        topic = socket.query_params.get('name', '')
        types = dict(ros.get_topic_names_and_types()).get(topic, [])
        if len(types) != 1:
            await socket.send_json({'error': 'Topic is unavailable or has multiple message types'})
            await socket.close(code=1008)
            return
        latest = [None, 0.0, 0]
        def update(message):
            latest[:] = [message, time.monotonic(), latest[2] + 1]
        subscription = None
        try:
            subscription = ros.create_subscription(get_message(types[0]), topic, update,
                QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT))
            while True:
                message, received, count = latest
                data = dict(message_to_ordereddict(message)) if message is not None else None
                encoded = json.dumps(data, default=str)
                if len(encoded) > 65536:
                    data = {'truncated': True, 'preview': encoded[:65536]}
                await socket.send_json({'topic': topic, 'type': types[0], 'count': count,
                                        'age_ms': round((time.monotonic() - received) * 1000) if received else None, 'data': data})
                try:
                    event = await asyncio.wait_for(socket.receive(), timeout=0.1)
                    if event['type'] == 'websocket.disconnect':
                        break
                except asyncio.TimeoutError:
                    pass
        except (WebSocketDisconnect, RuntimeError):
            pass
        finally:
            if subscription is not None:
                ros.destroy_subscription(subscription)

    @app.get('/api/recordings')
    async def list_recordings():
        active = ros.state.get('recording_file') if ros.state.get('recording') else None
        return [{'name': p.name, 'bytes': p.stat().st_size, 'active': p.name == active}
                for p in sorted(recordings.glob('*.csv'), key=lambda p: p.stat().st_mtime_ns, reverse=True)]

    @app.get('/api/recordings/{name}')
    async def download(name: str):
        if Path(name).name != name or not name.endswith('.csv'):
            raise HTTPException(404, 'Recording not found')
        if ros.state.get('recording') and ros.state.get('recording_file') == name:
            raise HTTPException(409, 'Stop the recording before downloading')
        file = recordings / name
        if not file.is_file():
            raise HTTPException(404, 'Recording not found')
        return FileResponse(file, media_type='text/csv', filename=name)

    @app.get('/api/system')
    async def system():
        return {'nodes': sorted(set(ros.get_node_names())),
                'topics': [{'name': n, 'types': t} for n, t in ros.get_topic_names_and_types()],
                'load_average': os.getloadavg(), 'cpu_count': os.cpu_count(),
                'kernel': os.uname().release, 'ros_distro': os.environ.get('ROS_DISTRO', 'unknown'),
                'usage': status.snapshot()}

    if static.is_dir():
        app.mount('/', StaticFiles(directory=static, html=True), name='ui')
    return app


def main():
    import uvicorn
    rclpy.init()
    ros = RosClient()
    spin = threading.Thread(target=rclpy.spin, args=(ros,), daemon=True)
    spin.start()
    root = Path(os.environ.get('DIAMOND_ROOT', Path.cwd())).resolve()
    recordings = root / 'recordings'
    recordings.mkdir(exist_ok=True)
    app = create_app(ros, recordings, root / 'web_ui/dist', load_project(root))
    try:
        uvicorn.run(app, host='0.0.0.0', port=int(os.environ.get('DIAMOND_PORT', '8000')),
                    access_log=False, log_level='warning')
    finally:
        rclpy.shutdown()
        spin.join(timeout=3)
        ros.destroy_node()
