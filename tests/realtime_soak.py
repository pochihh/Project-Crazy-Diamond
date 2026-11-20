#!/usr/bin/env python3
"""Run on the bench Pi: bounded two-axis motion, CSV and optional CPU/UI load."""
import argparse
import asyncio
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import threading
import time
from urllib.request import Request, urlopen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--physical', action='store_true', required=True)
    parser.add_argument('--seconds', type=float, default=1800)
    parser.add_argument('--load', action='store_true')
    parser.add_argument('--runner', action='store_true', help='Replay a Phantom sine or saved CSV and exercise live data previews')
    parser.add_argument('--csv-profile', help='Replay this saved data/programs CSV unchanged (requires --runner)')
    parser.add_argument('--verify-motion', action='store_true',
                        help='Require measured travel; sine mode uses the proven ±5 mm / 8 s waveform')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert math.isfinite(args.seconds) and args.seconds > 0
    if args.verify_motion and args.seconds < 16:
        parser.error('--verify-motion requires at least 16 seconds')
    period_s = 8.0 if args.verify_motion else .25
    amplitude_mm = 5.0 if args.verify_motion else .3
    phase_deg = 180.0
    period_ticks = round(period_s*2000)
    expected = None
    profile_sha256 = None
    motion_thresholds = [(-2.5, 2.5)]*2
    profile_ranges = None
    if args.csv_profile:
        if not args.runner or Path(args.csv_profile).name != args.csv_profile or not args.csv_profile.endswith('.csv'):
            parser.error('--csv-profile needs --runner and a CSV basename')
        profile = Path(__file__).resolve().parents[1]/'data/programs'/args.csv_profile
        profile_sha256 = hashlib.sha256(profile.read_bytes()).hexdigest()
        with profile.open() as source:
            reader = csv.DictReader(line for line in source if not line.startswith('#'))
            assert set(reader.fieldnames or []) == {'axis0_reference_mm', 'axis1_reference_mm', 'do0'}
            expected = [{k: float(v) for k, v in row.items()} for row in reader]
        assert expected and all(math.isfinite(v) for row in expected for v in row.values())
        assert all(row['do0'] in (0, 1) for row in expected)
        period_ticks = len(expected)
        period_s = period_ticks/2000
        profile_ranges = [(min(row[f'axis{a}_reference_mm'] for row in expected),
                           max(row[f'axis{a}_reference_mm'] for row in expected)) for a in range(2)]
        if args.verify_motion:
            assert all(hi-lo > 0 for lo, hi in profile_ranges), 'Both axes must have moving CSV targets'
            assert args.seconds >= 2*period_s, 'Motion verification needs two profile periods'
        motion_thresholds = [(lo+.25*(hi-lo), hi-.25*(hi-lo)) for lo, hi in profile_ranges]
    motion_check_s = max(16, 2*period_s) if args.runner else 16
    args.output.mkdir(parents=True, exist_ok=True)
    url = 'http://127.0.0.1:8000'

    def get():
        with urlopen(url+'/api/state', timeout=10) as response:
            return json.load(response)

    def command(operation, axis=0, **fields):
        request = Request(url+'/api/command', data=json.dumps(dict(operation=operation, axis=axis, **fields)).encode(),
                          headers={'Content-Type': 'application/json'})
        with urlopen(request, timeout=10) as response:
            return json.load(response)['state']

    def phantom(operation, **fields):
        request = Request('http://127.0.0.1:8001/command',
                          data=json.dumps(dict(operation=operation, **fields)).encode(),
                          headers={'Content-Type': 'application/json'})
        with urlopen(request, timeout=30) as response:
            return json.load(response)

    initial = get()
    assert initial['bridge_alive'] and initial['connected'] and not initial['mock']
    assert not initial['recording'] and not initial['fault'], initial['fault']
    if expected is not None:
        assert all(initial['minimum_mm'][a] <= lo <= hi <= initial['maximum_mm'][a]
                   for a, (lo, hi) in enumerate(profile_ranges)), 'CSV targets exceed configured travel'
    (args.output/'initial.json').write_text(json.dumps(initial, indent=2))
    counters = ('telemetry_errors', 'deadline_misses', 'dropped_samples',
                'dsp.invalid_frames', 'dsp.rejected_commands', 'dsp.command_timeout_count')
    def counter(s, key):
        return s['dsp'][key[4:]] if key.startswith('dsp.') else s[key]

    workers = []
    stream_stop = threading.Event()
    stream_counts = [0]*8
    stream_errors = []
    recording = False
    stream_thread = None

    async def streams():
        from wsproto import ConnectionType, WSConnection
        from wsproto.events import Request as Handshake, TextMessage, Ping, CloseConnection
        async def consume(index):
            writer = None
            try:
                reader, writer = await asyncio.open_connection('127.0.0.1', 8000)
                socket = WSConnection(ConnectionType.CLIENT)
                target = '/ws/data' if args.runner and index >= 4 else '/ws'
                writer.write(socket.send(Handshake(host='127.0.0.1:8000', target=target)))
                while not stream_stop.is_set():
                    data = await asyncio.wait_for(reader.read(65536), 5)
                    if not data: raise RuntimeError('UI stream closed')
                    socket.receive_data(data)
                    for event in socket.events():
                        if isinstance(event, TextMessage) and event.message_finished:
                            stream_counts[index] += 1
                        elif isinstance(event, Ping):
                            writer.write(socket.send(event.response()))
                        elif isinstance(event, CloseConnection):
                            raise RuntimeError('UI stream closed')
                writer.write(socket.send(CloseConnection(code=1000)))
                await writer.drain()
            except Exception as error:
                if not stream_stop.is_set():
                    stream_errors.append(str(error))
            finally:
                if writer:
                    writer.close()
                    await writer.wait_closed()
        await asyncio.gather(*(consume(i) for i in range(len(stream_counts))))

    try:
        command('stop')
        for a in range(2):
            command('mode', a, value=0)
            command('zero', a)
            command('limit', a, value=min(.25, initial['duty_limit'][a]))
            command('axis', a, enabled=True)
        if args.runner:
            with urlopen(url+'/api/data/signals', timeout=10) as response:
                signals = [item['name'] for item in json.load(response)['signals']]
            if args.csv_profile:
                phantom('start', source='csv', filename=args.csv_profile, completion='loop', signals=signals)
            else:
                phantom('start', source='sine', period_s=period_s, amplitude_mm=[amplitude_mm]*2,
                        offset_mm=[0,0], phase_deg=phase_deg, pulse_width_ms=5, completion='loop', signals=signals)
        else:
            command('system', enabled=True)
            command('record_start')
        recording = True
        if args.load:
            import wsproto  # The UI server already uses this installed package.
            for cpu in range(3):
                workers.append(subprocess.Popen(['taskset', '-c', str(cpu), sys.executable, '-c',
                    'x=1\nwhile True: x=(x*1664525+1013904223)&0xffffffff']))
            stream_thread = threading.Thread(target=lambda: asyncio.run(streams()))
            stream_thread.start()
        started = time.monotonic()
        report_at = started
        motion_min = [float('inf')]*2
        motion_max = [float('-inf')]*2
        with (args.output/'states.jsonl').open('w') as log:
            while time.monotonic()-started < args.seconds:
                phase = (time.monotonic()-started)*2*math.pi/8
                target = 5*math.sin(phase)
                if args.runner:
                    state = get()
                    assert state['runner_state'] == 1 and state['recording']
                else:
                    command('target', 0, value=target)
                    state = command('target', 1, value=-target)
                log.write(json.dumps(state)+'\n'); log.flush()
                assert state['bridge_alive'] and state['connected'] and not state['fault'], state['fault']
                assert state['system_enabled'] and all(state['axis_enabled'][:2])
                for key in counters:
                    assert counter(state,key) == counter(initial,key), f'{key}: {counter(initial,key)} -> {counter(state,key)}'
                assert not stream_errors, stream_errors
                for a in range(2):
                    motion_min[a] = min(motion_min[a], state['position_mm'][a])
                    motion_max[a] = max(motion_max[a], state['position_mm'][a])
                if args.verify_motion and time.monotonic()-started >= motion_check_s:
                    assert all(lo < bounds[0] and hi > bounds[1]
                               for lo, hi, bounds in zip(motion_min, motion_max, motion_thresholds)), \
                        f'Insufficient encoder motion: {list(zip(motion_min, motion_max))}'
                if time.monotonic() >= report_at:
                    print(json.dumps(dict(elapsed_s=round(time.monotonic()-started, 1), sample=state['sample_count'],
                        max_cycle_us=state['max_cycle_us'], max_lateness_us=state['max_lateness_us'],
                        counters={k:counter(state,k)-counter(initial,k) for k in counters})), flush=True)
                    report_at = time.monotonic()+30
                time.sleep(.1)
        if args.runner: phantom('stop')
        command('stop')
        state = command('record_stop'); recording = False
        for _ in range(100):
            state = get()
            if not state['recording']: break
            time.sleep(.05)
        assert not state['recording']
        stream_stop.set()
        for worker in workers: worker.terminate()
        for worker in workers: worker.wait(timeout=5)
        if stream_thread: stream_thread.join(timeout=7)
        assert not stream_errors, stream_errors
        if args.load: assert min(stream_counts) > 0, stream_counts
        path = Path(__file__).resolve().parents[1]/'recordings'/state['recording_file']
        rows = 0; previous = None; previous_tick = None
        recorded_min = [float('inf')]*2
        recorded_max = [float('-inf')]*2
        with path.open() as source:
            reader = csv.DictReader(line for line in source if not line.startswith('#'))
            for row in reader:
                assert None not in row and None not in row.values(), 'CSV shape'
                current = (int(row['sample']), int(row['timestamp_us']))
                if args.runner:
                    tick = int(row['runner_row'])
                    assert 0 <= tick < period_ticks
                    if expected is not None:
                        assert int(row['do0_command']) == expected[tick]['do0']
                        for a in range(2):
                            assert abs(float(row[f'axis{a}_target_mm'])-expected[tick][f'axis{a}_reference_mm']) < 1e-9
                    else:
                        assert int(row['do0_command']) == int(tick < 10)
                    if previous_tick is not None: assert tick == (previous_tick+1) % period_ticks
                    else: assert tick == 0
                    previous_tick = tick
                if previous:
                    assert current[0] == previous[0]+1 and current[1] > previous[1], (previous, current)
                for a in range(2):
                    position = float(row[f'axis{a}_position_mm'])
                    recorded_min[a] = min(recorded_min[a], position)
                    recorded_max[a] = max(recorded_max[a], position)
                previous = current; rows += 1
        assert rows == state['recorded_samples'] and rows > args.seconds*1900, rows
        if args.verify_motion:
            assert all(lo < bounds[0] and hi > bounds[1]
                       for lo, hi, bounds in zip(recorded_min, recorded_max, motion_thresholds)), 'No verified bidirectional motion'
        result = dict(result='PASS', duration_s=args.seconds, load=args.load, runner=args.runner, samples=rows,
                      verify_motion=args.verify_motion, measured_travel_mm=list(zip(recorded_min, recorded_max)),
                      source='csv' if args.csv_profile else 'sine' if args.runner else 'manual',
                      csv_profile=args.csv_profile, profile_sha256=profile_sha256, profile_ranges_mm=profile_ranges,
                      period_ticks=period_ticks if args.runner else None,
                      waveform=dict(amplitude_mm=amplitude_mm, period_s=period_s, phase_deg=phase_deg) if args.runner and not args.csv_profile else None,
                      recording=state['recording_file'], stream_messages=stream_counts,
                      max_cycle_us=state['max_cycle_us'], max_lateness_us=state['max_lateness_us'],
                      counters={k:counter(state,k)-counter(initial,k) for k in counters})
        (args.output/'result.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2), flush=True)
    finally:
        stream_stop.set()
        for worker in workers:
            if worker.poll() is None: worker.terminate()
        for worker in workers: worker.wait(timeout=5)
        if stream_thread: stream_thread.join(timeout=7)
        command('stop')
        if recording: command('record_stop')
        for a in range(2):
            command('mode', a, value=initial['control_mode'][a])
            command('limit', a, value=initial['duty_limit'][a])
        (args.output/'final.json').write_text(json.dumps(get(), indent=2))


if __name__ == '__main__':
    main()
