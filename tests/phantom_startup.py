#!/usr/bin/env python3
"""Mock-only HTTP/ROS integration: slow zero approach, exact recording start, cancellation."""
import concurrent.futures
import csv
import io
import json
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

UI='http://127.0.0.1:18765'
ROBOT='http://127.0.0.1:18766'


def get(base,path):
    with urlopen(base+path,timeout=5) as response:return json.load(response)


def post(base,path,body):
    request=Request(base+path,data=json.dumps(body).encode(),headers={'Content-Type':'application/json'})
    with urlopen(request,timeout=30) as response:return json.load(response)


def command(op,axis=0,**fields):
    return post(UI,'/api/command',dict(operation=op,axis=axis,**fields))


def wait(condition,seconds=10):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        value=condition()
        if value:return value
        time.sleep(.02)
    raise AssertionError('Condition timed out')


def state():return get(UI,'/api/state')


def stopped():
    s=state()
    return s if not s['recording'] and s['dsp']['arm_mask']==0 else None


def move_off_zero():
    command('stop')
    for axis,target in enumerate([3.,-2.]):
        command('mode',axis,value=0)
        command('profile_load',axis,profile='research_pid')
        command('axis',axis,enabled=True)
    command('system',enabled=True)
    for axis,target in enumerate([3.,-2.]):command('target',axis,value=target)
    wait(lambda:all(abs(a-b)<.02 for a,b in zip(state()['position_mm'][:2],[3.,-2.])))
    command('stop')


def start(speed=2):
    return post(ROBOT,'/command',dict(operation='start',source='sine',period_s=2.,
                amplitude_mm=[1.,1.],offset_mm=[0.,0.],phase_deg=180.,pulse_width_ms=5.,
                approach_speed_mm_s=float(speed),completion='loop'))


initial=state()
assert initial['mock'] and initial['connected'] and not initial['recording']
reports=[]
try:
    move_off_zero()
    with concurrent.futures.ThreadPoolExecutor() as pool:
        future=pool.submit(start)
        observed=[]
        end=time.monotonic()+10
        while not future.done():
            assert time.monotonic()<end
            s=state()
            if s['program_name']=='phantom-approach-zero' and s['runner_state'] in (1,2):
                observed.append(s)
                assert not s['recording'] and s['dsp']['digital_out']==0
            time.sleep(.02)
        assert future.result()['accepted'] and len(observed)>10
    for a,b in zip(observed,observed[1:]):
        dt=(b['sample_count']-a['sample_count'])/2000
        assert all(abs(b['target_mm'][i]-a['target_mm'][i])<=2*dt+1e-8 for i in (0,1))
    wait(lambda:state()['recorded_samples']>4000)
    post(ROBOT,'/command',{'operation':'stop'})
    s=wait(stopped)
    with urlopen(UI+'/api/recordings/'+s['recording_file']) as response:
        rows=list(csv.DictReader(io.StringIO('\n'.join(l for l in response.read().decode().splitlines() if not l.startswith('#')))))
    assert len(rows)==s['recorded_samples'] and int(rows[0]['runner_row'])==0
    assert int(rows[0]['do0_command'])==1
    for a in (0,1):
        assert float(rows[0][f'axis{a}_target_mm'])==0
        assert abs(float(rows[0][f'axis{a}_position_mm']))<=.1
    assert all(int(b['sample'])==int(a['sample'])+1 for a,b in zip(rows,rows[1:]))
    reports.append({'approach':'PASS','samples':len(rows),'recording':s['recording_file']})
    for stop_via in ('robot','stop_all'):
        move_off_zero()
        old_file=state()['recording_file']
        with concurrent.futures.ThreadPoolExecutor() as pool:
            future=pool.submit(start,1)
            wait(lambda:state()['program_name']=='phantom-approach-zero' and state()['runner_state']==1)
            if stop_via=='robot':post(ROBOT,'/command',{'operation':'stop'})
            else:command('stop')
            try:future.result(timeout=10);raise AssertionError('Cancelled Start succeeded')
            except HTTPError as error:assert error.code==409
        time.sleep(.5);s=state()
        assert not any(s['axis_enabled'][:2]) and s['dsp']['arm_mask']==0 and not s['recording']
        assert s['recording_file']==old_file
        reports.append({'cancel_via':stop_via,'result':'PASS'})
    for fields in ({'phase_deg':90.},{'offset_mm':[1.,0.]},{'approach_speed_mm_s':0.}):
        before=state()
        try:
            post(ROBOT,'/command',dict(operation='start',source='sine',**fields))
            raise AssertionError('Invalid start accepted')
        except HTTPError as error:assert error.code==409
        after=state()
        assert after['program_id']==before['program_id'] and after['stop_count']==before['stop_count']
        assert after['dsp']['arm_mask']==0 and not after['recording']
    print(json.dumps({'result':'PASS','checks':reports,'invalid_start_rejected_without_motion':True},indent=2))
finally:command('stop')
