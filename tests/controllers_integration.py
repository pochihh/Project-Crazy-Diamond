#!/usr/bin/env python3
"""Exercise live tuning/profile isolation through HTTP and verify recorded snapshots."""
import argparse
import csv
import io
import json
from pathlib import Path
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

parser = argparse.ArgumentParser()
parser.add_argument('--url', default='http://127.0.0.1:18765')
parser.add_argument('--physical', action='store_true')
args = parser.parse_args()


def get(path):
    with urlopen(args.url + path, timeout=5) as response:
        return json.load(response)


def command(op, axis=0, **fields):
    request = Request(args.url + '/api/command', data=json.dumps(dict(operation=op, axis=axis, **fields)).encode(),
                      headers={'Content-Type': 'application/json'})
    with urlopen(request, timeout=5) as response:
        return json.load(response)['state']


def parameters(profile):
    return dict(zip(profile['parameter_names'], profile['parameter_values']))


def rejected(op, axis=0, **fields):
    try:
        command(op, axis, **fields)
        raise AssertionError(f'{op} was incorrectly accepted')
    except HTTPError as error:
        assert error.code == 409, error.code


initial = get('/api/state')
assert initial['connected'] and initial['bridge_alive'] and not initial['recording']
assert initial['mock'] != args.physical
original = initial['controllers']
name = 'test_live_' + str(time.time_ns())
recording = False
try:
    command('stop'); command('clear')
    for a in range(2):
        command('mode', a, value=0)
    state = get('/api/state')
    state = command('profile_create', profile=name, controller_revision=state['controller_revision'][0])
    command('profile_load', 1, profile=name)
    baseline = parameters(state['controllers'][0])
    changed = {**baseline, 'kp': baseline['kp'] * 1.1, 'ki': baseline['ki'] * 1.1}
    for a in range(2):
        command('axis', a, enabled=True)
    command('system', enabled=True)
    command('record_start'); recording = True
    command('target', 0, value=.5); command('target', 1, value=-.5)
    time.sleep(.4)
    state = command('controller_apply', controller_type='pid', parameters=changed)
    assert state['system_enabled'] and all(state['axis_enabled'])
    assert parameters(state['controllers'][0]) == changed and parameters(state['controllers'][1]) == baseline
    assert state['controllers'][0]['id'] == name
    state = command('profile_save', profile=name, controller_revision=state['controller_revision'][0])
    assert parameters(state['controllers'][1]) == baseline
    saved = next(p for p in get('/api/controllers')['profiles'] if p['id'] == name)
    assert parameters(saved) == changed
    state = command('profile_load', 1, profile=name)
    assert state['system_enabled'] and all(state['axis_enabled']) and parameters(state['controllers'][1]) == changed
    rejected('controller_apply', controller_type='pid', parameters={'kp': .001})
    rejected('controller_apply', controller_type='lqr', parameters=changed)
    rejected('profile_load', profile='../bench_pid')
    rejected('profile_save', profile=name, controller_revision=0)
    rejected('profile_create', profile=name, controller_revision=state['controller_revision'][0])
    rejected('limit', value=.5)
    time.sleep(2)
    state = get('/api/state')
    assert state['system_enabled'] and all(state['axis_enabled']) and not state['fault']
    assert abs(state['position_mm'][0]-.5)<.3 and abs(state['position_mm'][1]+.5)<.3
    for key in ('telemetry_errors','dropped_samples') + (('deadline_misses',) if args.physical else ()):
        assert state[key] == initial[key], (key, state[key], initial[key])
    command('stop')
    state = command('record_stop'); recording = False
    deadline = time.monotonic()+3
    while state['recording'] and time.monotonic()<deadline:
        time.sleep(.05); state=get('/api/state')
    assert not state['recording']
    with urlopen(args.url+'/api/recordings/'+state['recording_file']) as response:
        rows = list(csv.DictReader(io.StringIO('\n'.join(l for l in response.read().decode().splitlines() if not l.startswith('#')))))
    assert len(rows)>1000 and len(rows)==state['recorded_samples']
    assert all(None not in r and None not in r.values() for r in rows)
    for a in range(2):
        snapshots = [dict(part.split('=') for part in row[f'axis{a}_controller_parameters'].split(';')) for row in rows]
        snapshots = [{k:float(v) for k,v in p.items()} for p in snapshots]
        assert changed in snapshots
        assert all(p==baseline or p==changed for p in snapshots), 'Partial controller update in recording'
        assert all(row[f'axis{a}_controller_type']=='pid' for row in rows)
    assert all(int(b['sample'])==int(a['sample'])+1 for a,b in zip(rows, rows[1:]))
    print(json.dumps(dict(result='PASS', physical=args.physical, samples=len(rows), recording=state['recording_file'],
                         live_tuning=True, shared_profile_isolation=True, explicit_reload=True,
                         max_cycle_us=state['max_cycle_us'], max_lateness_us=state['max_lateness_us'],
                         telemetry_error_delta=state['telemetry_errors']-initial['telemetry_errors'],
                         deadline_delta=state['deadline_misses']-initial['deadline_misses'],
                         dropped_delta=state['dropped_samples']-initial['dropped_samples']),indent=2))
finally:
    command('stop')
    if recording:
        command('record_stop')
    for a,p in enumerate(original):
        command('profile_load', a, profile=p['id'])
        command('controller_apply', a, controller_type=p['type'], parameters=parameters(p))
        command('mode', a, value=initial['control_mode'][a])
    path = Path(__file__).resolve().parents[1]/'projects'/initial['project_id']/'controllers'/(name+'.yaml')
    path.unlink(missing_ok=True)
