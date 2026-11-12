#!/usr/bin/env python3
"""Bounded end-to-end control/recording check; always leaves motors disabled."""
import argparse
import csv
import io
import json
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', default='http://127.0.0.1:8000')
    parser.add_argument('--physical', action='store_true', help='Allow the bounded ±2 mm motor test')
    parser.add_argument('--pwm', action='store_true', help='Also check short ±3% direct-duty pulses')
    parser.add_argument('--sweep', action='store_true', help='Also exercise ±50 mm and record for at least 60 seconds')
    args = parser.parse_args()

    def get(path):
        with urlopen(args.url + path, timeout=5) as response:
            return json.load(response)

    def command(operation, axis=0, value=0, enabled=False):
        body = json.dumps(dict(operation=operation, axis=axis, value=value, enabled=enabled)).encode()
        request = Request(args.url + '/api/command', data=body, headers={'Content-Type': 'application/json'})
        with urlopen(request, timeout=5) as response:
            return json.load(response)

    def wait(predicate, seconds=8):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            state = get('/api/state')
            assert state.get('bridge_alive') and state.get('connected'), state
            assert not state.get('fault'), state['fault']
            assert state['telemetry_errors'] == initial['telemetry_errors'], 'Telemetry error during regression'
            assert state['dropped_samples'] == initial['dropped_samples'], 'Sample dropped during regression'
            if args.physical:
                assert state['deadline_misses'] == initial['deadline_misses'], 'Deadline miss during regression'
            if predicate(state):
                return state
            time.sleep(0.05)
        raise AssertionError(f'Condition timed out: {state}')

    initial = get('/api/state')
    assert initial.get('bridge_alive') and initial.get('connected'), 'Bridge must be connected before testing'
    assert initial['mock'] != args.physical, 'Use --physical only for an actual motor test'
    assert not initial.get('recording'), 'Finish the current recording before testing'
    original_limits = initial['duty_limit'][:2]
    recording = False
    try:
        command('stop')
        command('clear')
        for a in range(2):
            command('mode', a, 0)
            command('zero', a)
            command('limit', a, 0.25 if args.physical else 1.0)
            command('axis', a, enabled=True)
        command('system', enabled=True)
        command('target', 0, 2)
        command('target', 1, -2)
        wait(lambda s: abs(s['position_mm'][0]-2)<0.3 and abs(s['position_mm'][1]+2)<0.3)
        try:
            command('zero', 0)
            raise AssertionError('Reset incorrectly accepted while motor enabled')
        except HTTPError as e:
            assert e.code == 409
        rejections = get('/api/state')['input_rejections']
        assert command('target', 0, 51)['published']
        wait(lambda s: s['input_rejections'] > rejections and s['target_mm'][0] == 2)
        command('record_start'); recording = True
        recording_started = time.monotonic()
        endpoints = []
        if args.sweep:
            for targets in [(50, -50), (-50, 50), (0, 0)]:
                for a, value in enumerate(targets):
                    command('target', a, value)
                reached = wait(lambda s: all(abs(s['position_mm'][a]-targets[a])<0.3 for a in range(2)), 12)
                endpoints.append({'target_mm': targets, 'position_mm': reached['position_mm']})
            while time.monotonic() - recording_started < 60:
                wait(lambda s: s['recording'] and s['system_enabled'])
                time.sleep(1)
        command('target', 0, 1); command('target', 1, -1)
        wait(lambda s: abs(s['position_mm'][0]-1)<0.3 and abs(s['position_mm'][1]+1)<0.3)
        # No browser connection or command refresh is needed to hold position.
        time.sleep(3)
        held = wait(lambda s: abs(s['position_mm'][0]-1)<0.3 and abs(s['position_mm'][1]+1)<0.3)
        assert held['system_enabled'] and all(held['axis_enabled'][:2]) and held['recording']
        command('axis', 0, enabled=False)
        wait(lambda s: abs(s['dsp']['duty'][0])<0.001 and s['axis_enabled'][1])
        command('zero', 0)
        wait(lambda s: abs(s['position_mm'][0])<0.02 and s['target_mm'][0]==0)
        command('stop')
        pwm_checks = []
        if args.pwm:
            for a in range(2):
                command('mode', a, 1)
                command('pwm', a, 0.03)  # Enabling must clear preloaded output.
                command('axis', a, enabled=True)
                command('system', enabled=True)
                wait(lambda s: s['pwm_target'][a] == 0 and abs(s['dsp']['duty'][a]) < 0.001)
                try:
                    command('mode', a, 0)
                    raise AssertionError('Mode changed while enabled')
                except HTTPError as e:
                    assert e.code == 409
                for duty in (0.03, -0.03):
                    command('pwm', a, duty)
                    sample = wait(lambda s: abs(s['dsp']['duty'][a] - duty) < 0.001, 2)
                    time.sleep(0.15)
                    command('pwm', a, 0)
                    wait(lambda s: abs(s['dsp']['duty'][a]) < 0.001)
                    pwm_checks.append({'axis': a, 'duty': duty, 'position_mm': sample['position_mm'][a]})
                command('stop')
                command('mode', a, 0)
        command('record_stop'); recording = False
        final = wait(lambda s: not s['recording'] and not s['system_enabled'])
        with urlopen(args.url + '/api/recordings/' + final['recording_file']) as response:
            text = response.read().decode()
        rows = list(csv.DictReader(io.StringIO('\n'.join(line for line in text.splitlines() if not line.startswith('#')))))
        assert all(None not in row and None not in row.values() for row in rows), 'CSV header/row mismatch'
        if args.pwm:
            for a in range(2):
                assert any(row[f'axis{a}_mode'] == '1' and float(row[f'axis{a}_pwm_target']) == 0.03 for row in rows)
        assert len(rows) == final['recorded_samples'] and len(rows)>2500
        sequence = [int(row['sample']) for row in rows]
        assert all(b == a+1 for a,b in zip(sequence, sequence[1:])), 'Lost/duplicated recording samples'
        times = [int(row['timestamp_us']) for row in rows]
        assert all(b>a for a,b in zip(times,times[1:])), 'Timestamp did not advance'
        assert final['dropped_samples']==initial['dropped_samples']
        assert final['telemetry_errors']==initial['telemetry_errors'], 'Telemetry error during regression'
        if args.physical:
            assert final['deadline_misses']==initial['deadline_misses'], 'Deadline miss during regression'
        print(json.dumps({'result':'PASS','physical':args.physical,'sweep':endpoints,'pwm':pwm_checks,'recorded_samples':len(rows),
                          'recording':final['recording_file'],'max_cycle_us':final['max_cycle_us'],
                          'max_lateness_us':final['max_lateness_us'],'deadline_misses':final['deadline_misses'],
                          'telemetry_errors':final['telemetry_errors'],'dropped_samples':final['dropped_samples']},indent=2))
    finally:
        try:
            command('stop')
            if recording:
                command('record_stop')
            for a, value in enumerate(original_limits):
                command('mode', a, 0)
                command('limit', a, value)
        except Exception as error:
            print(f'Cleanup could not reach the bridge: {error}')


if __name__ == '__main__':
    main()
