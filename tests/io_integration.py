#!/usr/bin/env python3
"""Check I/O-only HTTP → ROS → DSP state and CSV; does not measure pin voltage."""
import argparse
import csv
import io
import json
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

parser = argparse.ArgumentParser()
parser.add_argument('--url', default='http://127.0.0.1:8000')
args = parser.parse_args()


def get(path):
    with urlopen(args.url + path, timeout=5) as response:
        return json.load(response)


def command(operation, axis=0, value=0, enabled=False):
    body = json.dumps(dict(operation=operation, axis=axis, value=value, enabled=enabled)).encode()
    with urlopen(Request(args.url + '/api/command', data=body,
                         headers={'Content-Type': 'application/json'}), timeout=5) as response:
        return json.load(response)


initial = get('/api/state')
assert initial['bridge_alive'] and initial['connected'] and not initial['recording']
assert not initial['system_enabled'] and initial['dsp']['arm_mask'] == 0


def wait(predicate):
    for _ in range(100):
        state = get('/api/state')
        assert state['bridge_alive'] and state['connected'] and not state['fault']
        assert state['dsp']['arm_mask'] == 0 and not any(state['axis_enabled'])
        for key in ('telemetry_errors', 'deadline_misses', 'dropped_samples'):
            if key == 'deadline_misses' and initial['mock']:
                continue  # Mock runs under ordinary scheduling, not SCHED_FIFO.
            assert state[key] == initial[key], (key, state[key], initial[key])
        if predicate(state):
            return state
        time.sleep(.02)
    raise AssertionError(state)


recording = False
try:
    assert command('digital', 3, 1)['published']
    wait(lambda s: s['input_rejections'] > initial['input_rejections'] and s['dsp']['digital_out'] == 0)
    command('system', enabled=True)
    wait(lambda s: s['dsp']['state'] == 1)
    command('record_start'); recording = True
    for bit in range(4):
        command('digital', bit, 1)
        wait(lambda s: s['dsp']['digital_out'] == (1 << (bit + 1)) - 1)
    command('analog', 0, 1024); command('analog', 1, 3072)
    wait(lambda s: s['dsp']['analog_out'] == [1024, 3072] and s['dsp']['analog_valid'] == 63)
    time.sleep(.2)
    command('stop')
    wait(lambda s: not s['system_enabled'] and s['dsp']['digital_out'] == 0 and s['dsp']['analog_out'] == [0, 0])
    command('record_stop'); recording = False
    final = wait(lambda s: not s['recording'])
    with urlopen(args.url + '/api/recordings/' + final['recording_file']) as response:
        lines = response.read().decode().splitlines()
    rows = list(csv.DictReader(io.StringIO('\n'.join(line for line in lines if not line.startswith('#')))))
    assert len(rows) == final['recorded_samples'] and len(rows) > 100
    assert any(r['digital_out'] == '15' and r['ao0_raw'] == '1024' and r['ao1_raw'] == '3072' for r in rows)
    for row in rows:
        assert None not in row and row['dsp_arm_mask'] == '0' and row['encoder_valid'] == '63'
        assert row['encoder0_counts'] == row['axis0_raw_counts'] and row['encoder1_counts'] == row['axis1_raw_counts']
    print(json.dumps(dict(result='PASS', samples=len(rows), recording=final['recording_file'],
                         physical_loopback_checked=False), indent=2))
finally:
    command('stop')
    if recording:
        command('record_stop')
