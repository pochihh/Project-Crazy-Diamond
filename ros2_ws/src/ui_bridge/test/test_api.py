from types import SimpleNamespace
from fastapi.testclient import TestClient
from ui_bridge.app import create_app
from ui_bridge.app import RosClient
from interfaces.msg import BridgeState


class FakeRos:
    state = {}
    client = SimpleNamespace(service_is_ready=lambda: False)
    catalog_client = SimpleNamespace(service_is_ready=lambda: False)

    def snapshot(self):
        return dict(self.state, bridge_alive=False)


def test_snapshots_order_http_and_websocket_without_changing_control_frames():
    ros = SimpleNamespace(sequence=0, instance='ui-process', state={}, received=0, robot={}, robot_received=0, results={})
    message = BridgeState()
    message.sample_count = 100
    RosClient.update(ros, message)
    first = RosClient.snapshot(ros)
    ros.results[1] = 'old bridge acknowledgement'
    message.sample_count = 1  # Host-controller restart still advances the UI sequence.
    RosClient.update(ros, message)
    second = RosClient.snapshot(ros)
    assert first['ui_instance'] == second['ui_instance'] == 'ui-process'
    assert first['ui_sequence'] == 1 and second['ui_sequence'] == 2
    assert first['sample_count'] == 100 and second['sample_count'] == 1
    assert second['bridge_alive']
    assert not ros.results


def test_offline_commands_and_input_bounds(tmp_path):
    client = TestClient(create_app(FakeRos(), tmp_path, tmp_path / 'missing-ui'))
    assert client.get('/api/state').json()['bridge_alive'] is False
    assert client.get('/api/controllers').status_code == 503
    assert client.post('/api/command', content='{"operation":"controller_apply","parameters":{"kp":1e999}}',
                       headers={'Content-Type':'application/json'}).status_code == 422
    assert client.post('/api/command', json={'operation': 'system', 'enabled': True}).status_code == 503
    assert client.post('/api/command', json={'operation': 'zero', 'axis': 6}).status_code == 422
    assert client.post('/api/command', json={'operation': 'digital', 'axis': 3}).status_code == 503
    assert client.post('/api/command', json={'operation': 'digital', 'axis': 4}).status_code == 422
    assert client.post('/api/command', json={'operation': 'analog', 'axis': 2}).status_code == 422
    assert client.post('/api/command', content='{"operation":"target","value":1e999}',
                       headers={'Content-Type': 'application/json'}).status_code == 422


def test_recording_download_contract(tmp_path):
    ros = FakeRos()
    ros.state = {'recording': True, 'recording_file': 'session-test.csv'}
    (tmp_path / 'session-test.csv').write_text('timestamp_us,position\n1,0\n')
    client = TestClient(create_app(ros, tmp_path, tmp_path / 'missing-ui'))
    assert client.get('/api/recordings').json()[0]['active'] is True
    assert client.get('/api/recordings/session-test.csv').status_code == 409
    ros.state['recording'] = False
    response = client.get('/api/recordings/session-test.csv')
    assert response.status_code == 200 and response.text.startswith('timestamp_us')
    assert client.get('/api/recordings/config.yaml').status_code == 404
    assert client.get('/api/recordings/session-missing.csv').status_code == 404


def test_recordings_sort_by_modification_time_across_filename_formats(tmp_path):
    import os
    old = tmp_path / 'session-old.csv'
    new = tmp_path / '2026-11-12_09-30-00.csv'
    old.write_text('old')
    new.write_text('new')
    os.utime(old, ns=(1, 1))
    os.utime(new, ns=(2, 2))
    client = TestClient(create_app(FakeRos(), tmp_path, tmp_path / 'missing-ui'))
    assert [entry['name'] for entry in client.get('/api/recordings').json()] == [new.name, old.name]


class TopicRos(FakeRos):
    destroyed = False

    def get_node_names(self):
        return ['bridge', 'ui_bridge']

    def get_topic_names_and_types(self):
        return [('/bridge/state', ['interfaces/msg/BridgeState'])]

    def create_subscription(self, message_type, topic, callback, qos):
        assert message_type is BridgeState and topic == '/bridge/state' and qos.depth == 1
        message = BridgeState()
        message.sample_count = 123
        callback(message)
        return 'subscription'

    def destroy_subscription(self, subscription):
        assert subscription == 'subscription'
        self.destroyed = True


def test_project_status_and_topic_stream_cleanup(tmp_path):
    ros = TopicRos()
    project = {'id': 'test', 'name': 'Test robot', 'axes': [{'name': 'X'}, {'name': 'Y'}]}
    with TestClient(create_app(ros, tmp_path, tmp_path / 'missing-ui', project)) as client:
        assert client.get('/api/project').json() == project
        with client.websocket_connect('/ws?hz=10') as socket:
            assert socket.receive_json()['bridge_alive'] is False
        info = client.get('/api/system').json()
        assert info['nodes'] == ['bridge', 'ui_bridge'] and 'disk' in info['usage']
        with client.websocket_connect('/ws/topics?name=/bridge/state') as socket:
            data = socket.receive_json()
            assert data['data']['sample_count'] == 123 and data['count'] == 1
        with client.websocket_connect('/ws/topics?name=/missing') as socket:
            assert 'error' in socket.receive_json()
    assert ros.destroyed


def test_invalid_stream_rate_and_project_selection(tmp_path, monkeypatch):
    import json
    import pytest
    from starlette.websockets import WebSocketDisconnect
    from ui_bridge.app import load_project
    client = TestClient(create_app(FakeRos(), tmp_path, tmp_path / 'missing-ui'))
    for rate in ('0', '1000', 'bad'):
        with pytest.raises(WebSocketDisconnect):
            with client.websocket_connect('/ws?hz=' + rate):
                pass
    monkeypatch.setenv('DIAMOND_PROJECT', '../elsewhere')
    with pytest.raises(ValueError):
        load_project(tmp_path)
    monkeypatch.setenv('DIAMOND_PROJECT', 'test')
    project = tmp_path / 'projects' / 'test'
    project.mkdir(parents=True)
    manifest = {'id': 'test', 'axes': [{}, {}]}
    (project / 'project.json').write_text(json.dumps(manifest))
    assert load_project(tmp_path) == manifest
    (project / 'project.json').write_text('{"id":"other","axes":[]}')
    with pytest.raises(ValueError):
        load_project(tmp_path)


def test_continuous_inputs_publish_without_waiting_for_service_or_state_ack(tmp_path):
    class OnlineRos(FakeRos):
        def __init__(self):
            self.sent = []
        def snapshot(self):
            return {'bridge_alive': True, 'connected': True}
        def publish_input(self, operation, channel, value):
            self.sent.append((operation, channel, value))
    ros = OnlineRos()
    client = TestClient(create_app(ros, tmp_path, tmp_path / 'missing-ui'))
    assert client.post('/api/command', json={'operation':'pwm','axis':5,'value':.2}).json()['published']
    assert client.post('/api/command', json={'operation':'target','axis':0,'value':7}).json()['published']
    assert ros.sent == [('pwm',5,.2),('target',0,7)]
    assert client.post('/api/command', json={'operation':'analog','axis':1,'value':1.5}).status_code == 422
    assert client.post('/api/command', json={'operation':'digital','axis':0,'value':2}).status_code == 422
    assert client.post('/api/command', json={'operation':'system','enabled':True}).status_code == 503


def test_dsp_fields_are_relayed_and_result_errors_are_kept_per_command():
    from interfaces.msg import CommandResult
    ros = SimpleNamespace(sequence=0, instance='ui-process', state={}, received=0, results={})
    message = BridgeState()
    message.dsp.session = 123
    message.dsp.state = 2
    message.dsp.command_timeout_count = 7
    message.dsp.duty = [.1,.2,.3,.4,.5,.6]
    RosClient.update(ros, message)
    assert ros.state['dsp']['session'] == 123
    assert ros.state['dsp']['state'] == 2
    assert ros.state['dsp']['command_timeout_count'] == 7
    assert len(ros.state['dsp']['duty']) == 6
    RosClient.result(ros, CommandResult(command_id=1,applied=False,message='Disable axis'))
    RosClient.result(ros, CommandResult(command_id=2,applied=True))
    assert not ros.results[1].applied and ros.results[2].applied


def test_data_services_relay_shared_commands_and_preview_batches(tmp_path):
    from collections import deque
    from interfaces.msg import CommandResult
    from interfaces.srv import LoggerCommand, RunnerCommand, LoadCsv, ListPrograms, SignalCatalog
    class Client:
        def __init__(self, result): self.response=result; self.requests=[]
        def service_is_ready(self): return True
        def call_async(self, request):
            self.requests.append(request)
            return SimpleNamespace(done=lambda:True,result=lambda:self.response)
    ros=FakeRos()
    ros.state={'last_command_id':19}
    ros.results={19:CommandResult(command_id=19,applied=True)}
    ros.data_clients={
        'logger':Client(LoggerCommand.Response(accepted=True,command_id=19)),
        'runner':Client(RunnerCommand.Response(accepted=True,command_id=19)),
        'load_csv':Client(LoadCsv.Response(accepted=True,command_id=19)),
        'programs':Client(ListPrograms.Response(names=['wave.csv'],sizes=[123])),
        'signals':Client(SignalCatalog.Response()),
    }
    ros.previews=deque([{'sequence':1,'minimum':[0.0],'maximum':[1.0]},
                        {'sequence':2,'minimum':[0.0],'maximum':[0.0]}])
    with TestClient(create_app(ros,tmp_path,tmp_path/'missing-ui')) as client:
        assert client.get('/api/data/programs').json()['names']==['wave.csv']
        assert client.post('/api/data/logger',json={'operation':'start','signals':['do0']}).status_code==200
        assert list(ros.data_clients['logger'].requests[-1].signals)==['do0']
        assert client.post('/api/data/runner',json={'operation':'start','completion':'loop','record':True}).status_code==200
        request=ros.data_clients['runner'].requests[-1]
        assert request.completion=='loop' and request.record and request.auto_enable
        assert client.post('/api/data/load_csv',json={'name':'wave.csv'}).status_code==200
        assert ros.data_clients['load_csv'].requests[-1].csv==''
        with client.websocket_connect('/ws/data') as socket:
            frames=socket.receive_json()['frames']
            assert len(frames)==2 and frames[0]['maximum']==[1.0]


def test_preview_restart_and_robot_expiry():
    import time
    from collections import deque
    from interfaces.msg import DataPreview
    ros=SimpleNamespace(preview={},previews=deque(maxlen=100))
    RosClient.update_preview(ros,DataPreview(sequence=100))
    RosClient.update_preview(ros,DataPreview(sequence=1))
    assert len(ros.previews)==1
    ros=SimpleNamespace(state={},received=time.monotonic(),robot={'id':'test'},robot_received=time.monotonic())
    assert RosClient.snapshot(ros)['robot']=={'id':'test'}
    ros.robot_received-=3
    assert RosClient.snapshot(ros)['robot'] is None


def test_controller_apply_verifies_rt_readback_not_request_or_latest_state(tmp_path):
    from interfaces.msg import CommandResult, ControllerProfile
    from interfaces.srv import Command

    class OnlineRos(FakeRos):
        def __init__(self):
            self.state = {'last_command_id': 8, 'controllers': [{'type': 'later-setting'}]}
            self.results = {}
            self.actual = ControllerProfile(id='working', type='pid', parameter_names=['kd','kp','ki'],
                                            parameter_values=[.0001,.001,0.], slew_rate_mm_s=100.)
            self.has_readback = True
            self.applied = True
            self.client = SimpleNamespace(service_is_ready=lambda:True, call_async=self.call)
        def snapshot(self): return dict(self.state, bridge_alive=True)
        def call(self, request):
            self.request = request
            self.results[7] = CommandResult(command_id=7, applied=self.applied, message='Rejected by RT',
                has_controller=self.has_readback, axis=1, controller_revision=42,
                controller=self.actual, duty_limit=.5)
            return SimpleNamespace(done=lambda:True, result=lambda:Command.Response(accepted=True, command_id=7))

    ros = OnlineRos()
    client = TestClient(create_app(ros, tmp_path, tmp_path/'missing-ui'))
    request = dict(operation='controller_apply', axis=1, controller_type='pid',
                   parameters=dict(kp=.001,ki=0.,kd=.0001), set_reference=True,
                   slew_enabled=False, slew_rate_mm_s=100.)
    response = client.post('/api/command', json=request)
    assert response.status_code == 200
    readback = response.json()['controller_readback']
    assert readback['controller_revision'] == 42 and readback['axis'] == 1
    assert readback['controller']['parameter_values'] == [.0001,.001,0.]
    assert response.json()['state']['controllers'][0]['type'] == 'later-setting'
    assert list(ros.request.parameter_names) == ['kp','ki','kd']
    ros.actual.parameter_values = [.0002,.001,0.]
    assert client.post('/api/command', json=request).status_code == 409
    ros.actual.parameter_values = [.0001,.001,0.]
    ros.actual.slew_enabled = True
    assert client.post('/api/command', json=request).status_code == 409
    ros.has_readback = False
    assert client.post('/api/command', json=request).status_code == 502
    ros.has_readback = True
    assert client.post('/api/command', json=dict(operation='limit',axis=1,value=.5)).status_code == 200
    assert client.post('/api/command', json=dict(operation='limit',axis=1,value=.4)).status_code == 409
    ros.applied = False
    response = client.post('/api/command', json=request)
    assert response.status_code == 409 and response.json()['detail'] == 'Rejected by RT'
