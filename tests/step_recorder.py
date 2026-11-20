#!/usr/bin/env python3
"""Run the complete Step Recorder experiment against an isolated mock bridge."""
import argparse
import csv
import json
import math
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen


def request(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    with urlopen(Request(base + path, data=data, headers={'Content-Type': 'application/json'}), timeout=6) as response:
        return json.load(response)


def command(operation, **fields):
    return request('/api/command', {'operation': operation, 'axis': 0, **fields})['state']


def expect_rejected(operation, **fields):
    try:
        command(operation, **fields)
    except HTTPError as error:
        assert error.code == 409, error
    else:
        raise AssertionError(f'{operation} unexpectedly accepted')


def completed():
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = request('/api/state')
        if state['runner_state'] == 3 and not state['recording'] and not state['axis_enabled'][0]:
            break
        time.sleep(.025)
    else:
        raise AssertionError('Step did not stop and save')
    assert not state['fault'] and not state['dsp']['arm_mask']
    with urlopen(base + '/api/recordings/' + state['recording_file'], timeout=5) as csv_file:
        rows = list(csv.DictReader(line.decode() for line in csv_file if not line.startswith(b'#')))
    assert len(rows) == state['recorded_samples']
    return rows


parser = argparse.ArgumentParser()
parser.add_argument('--url', required=True)
base = parser.parse_args().url.rstrip('/')
initial = request('/api/state')
assert initial['mock'] and initial['connected'] and not initial['recording'] and not any(initial['axis_enabled'])
assert 'step_recorder' in [item['type'] for item in request('/api/controllers')['types']]
original_profile = initial['controllers'][0]['id']
try:
    command('profile_load', profile='step_recorder')
    for gain in (0, .0005):
        params = dict(kp=gain, step_duty=-.12, baseline_s=.025, step_s=.050, recovery_s=.020)
        state = command('controller_apply', controller_type='step_recorder', parameters=params)
        revision = state['controller_revision'][0]
        expect_rejected('step_start', controller_revision=revision-1)
        command('step_start', controller_revision=revision)
        rows = completed()
        assert len(rows) == 190
        for tick, row in enumerate(rows):
            assert int(row['step_tick']) == tick and int(row['ack_matched']) == int(row['axis0_ctrl_valid']) == 1
            assert row['ctrl_input_session'] == row['dsp_session']
            assert row['axis0_ctrl_type'] == 'step_recorder' and int(row['axis0_ctrl_revision']) == revision
            assert int(row['ctrl_input_sequence']) < int(row['dsp_sequence'])
            assert 0 < int(row['ack_host_tick']) <= int(row['host_tick'])
            assert float(row['axis0_ctrl_r_mm']) == 0
            injection = -.12 if 50 <= tick < 150 else 0.
            assert float(row['axis0_ctrl_u_im_duty']) == injection
            feedback = -gain * float(row['axis0_ctrl_error_counts'])
            assert math.isclose(feedback, float(row['axis0_ctrl_u_duty']), abs_tol=1e-10)
            duty = max(-1., min(1., feedback + injection))
            for field in ('axis0_ctrl_u_g_duty', 'axis0_requested_duty', 'axis0_output_duty'):
                assert math.isclose(float(row[field]), duty, abs_tol=1e-7)
        assert max(abs(float(row['axis0_position_mm'])) for row in rows) > .01
    state = command('controller_apply', controller_type='step_recorder', parameters={**params, 'step_s': 4})
    revision = state['controller_revision'][0]
    request('/api/data/logger', {'operation': 'start', 'signals': ['axis0_position_mm']})
    expect_rejected('step_start', controller_revision=revision)
    assert request('/api/state')['recording']
    request('/api/data/logger', {'operation': 'stop'})
    command('step_start', controller_revision=revision)
    command('step_stop')
    partial = completed()
    assert 0 < len(partial) < 8090
    print(f'PASS: 190 ticks each with Kp=0 and Kp=.0005, exact phase boundaries, aligned duty, recording ownership, early stop after {len(partial)} ticks')
finally:
    command('stop')
    command('profile_load', profile=original_profile)
