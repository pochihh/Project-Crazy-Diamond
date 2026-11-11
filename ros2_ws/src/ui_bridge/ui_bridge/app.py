"""Trusted-LAN API. Browser lifetime never owns motor or recording lifetime."""
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
from interfaces.msg import BridgeState
from interfaces.srv import Command, ControllerCatalog
from ui_bridge.system_status import SystemStatus


class RosClient(Node):
    def __init__(self):
        super().__init__('ui_bridge')
        self.state = {}
        self.received = 0.0
        self.sequence = 0
        self.instance = uuid.uuid4().hex
        self.client = self.create_client(Command, '/bridge/command')
        self.catalog_client = self.create_client(ControllerCatalog, '/bridge/controllers')
        self.subscription = self.create_subscription(BridgeState, '/bridge/state', self.update, 1)

    def update(self, message):
        self.sequence += 1
        self.state = dict(message_to_ordereddict(message), ui_sequence=self.sequence, ui_instance=self.instance)
        self.received = time.monotonic()

    def snapshot(self):
        state = self.state.copy()
        age = time.monotonic() - self.received
        state.update(bridge_alive=age < 1.0, state_age_ms=round(age * 1000) if self.received else None)
        return state


class Request(BaseModel):
    operation: str
    axis: int = Field(default=0, ge=0, le=5)
    value: float = 0.0
    enabled: bool = False
    controller_type: str = ''
    profile: str = ''
    parameters: dict[str, float] = Field(default_factory=dict)
    controller_revision: int = Field(default=0, ge=0)


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

    @app.post('/api/command')
    async def command(body: Request):
        if body.axis >= (4 if body.operation == 'digital' else 2):
            raise HTTPException(422, 'Invalid axis or I/O channel')
        if not math.isfinite(body.value):
            raise HTTPException(422, 'Value must be finite')
        if len(body.parameters) > 16 or any(not math.isfinite(v) for v in body.parameters.values()):
            raise HTTPException(422, 'Controller parameters must be finite; at most 16 are supported')
        if not ros.snapshot().get('bridge_alive') or not ros.client.service_is_ready():
            raise HTTPException(503, 'Control bridge is unavailable')
        async with command_lock:
            request = Command.Request()
            request.operation, request.axis = body.operation, body.axis
            request.value, request.enabled = body.value, body.enabled
            request.controller_type, request.profile = body.controller_type, body.profile
            request.parameter_names = list(body.parameters)
            request.parameter_values = list(body.parameters.values())
            request.controller_revision = body.controller_revision
            future = ros.client.call_async(request)
            deadline = time.monotonic() + 3
            while not future.done():
                if time.monotonic() > deadline:
                    raise HTTPException(504, 'Bridge command timed out')
                await asyncio.sleep(0.005)
            result = future.result()
            if not result.accepted:
                raise HTTPException(409, result.message)
            while ros.state.get('last_command_id', 0) < result.command_id:
                if time.monotonic() > deadline:
                    raise HTTPException(504, 'Command acknowledgment timed out')
                await asyncio.sleep(0.005)
            if ros.state.get('last_command_id') == result.command_id and ros.state.get('command_error'):
                raise HTTPException(409, ros.state['command_error'])
            return {'command_id': result.command_id, 'state': ros.snapshot()}

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
                for p in sorted(recordings.glob('session-*.csv'), reverse=True)]

    @app.get('/api/recordings/{name}')
    async def download(name: str):
        if Path(name).name != name or not name.startswith('session-') or not name.endswith('.csv'):
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
