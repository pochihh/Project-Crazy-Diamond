#!/usr/bin/env python3
"""Run on the bench Pi: bounded two-axis motion, CSV and optional CPU/UI load."""
import argparse
import asyncio
import csv
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
    parser.add_argument('--runner', action='store_true', help='Replay a Phantom sine and exercise live data previews')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.seconds > 0
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
            phantom('start', source='sine', period_s=.25, amplitude_mm=[.3,.3],
                    offset_mm=[0,0], phase_deg=180, pulse_width_ms=5, completion='loop', signals=signals)
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
        with path.open() as source:
            reader = csv.DictReader(line for line in source if not line.startswith('#'))
            for row in reader:
                assert None not in row and None not in row.values(), 'CSV shape'
                current = (int(row['sample']), int(row['timestamp_us']))
                if args.runner:
                    tick = int(row['runner_row'])
                    assert int(row['do0_command']) == int(tick < 10)
                    if previous_tick is not None: assert tick == (previous_tick+1) % 500
                    else: assert tick == 0
                    previous_tick = tick
                if previous:
                    assert current[0] == previous[0]+1 and current[1] > previous[1], (previous, current)
                previous = current; rows += 1
        assert rows == state['recorded_samples'] and rows > args.seconds*1900, rows
        result = dict(result='PASS', duration_s=args.seconds, load=args.load, runner=args.runner, samples=rows,
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
