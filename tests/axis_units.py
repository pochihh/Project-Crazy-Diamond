#!/usr/bin/env python3
"""Verify a mock project with a reversed linear axis 0 and reversed rotary axis 2."""
import argparse
import csv
import json
import math
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import JointState

parser = argparse.ArgumentParser()
parser.add_argument('--url', required=True)
url = parser.parse_args().url.rstrip('/') + '/api/'


def api(path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = Request(url + path, data=data, headers={'Content-Type': 'application/json'})
    return json.load(urlopen(req, timeout=6))


def command(op, **fields):
    return api('command', {'operation': op, 'axis': 2, **fields})


def wait(predicate):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        state = api('state')
        if predicate(state):
            return state
        time.sleep(.025)
    raise AssertionError('Timed out waiting for bridge state')


s = api('state')
assert s['mock'] and s['connected'] and not any(s['axis_enabled']) and not s['recording']
assert s['position_unit'][:3] == ['mm','mm','deg'] and all(s['reverse_direction'][:3])
rclpy.init()
node = rclpy.create_node('verify_axis_coordinates')
joints = []
node.create_subscription(JointState, '/joint_states', joints.append, qos_profile_sensor_data)
try:
    command('axis', enabled=True)
    command('system', enabled=True)
    command('target', unit='deg', value=45)
    state = wait(lambda s: abs(s['position'][2]-45) < .05)
    assert state['target'][2] == 45 and state['position_mm'][2] == 0
    assert state['dsp']['position'][2] < state['zero_counts'][2]
    assert state['dsp']['reference'][2] == state['zero_counts'][2] - round(45*state['counts_per_unit'][2])
    rejections = state['input_rejections']
    command('target', unit='mm', value=90)
    state = wait(lambda s: s['input_rejections'] > rejections)
    assert state['target'][2] == 45
    command('stop')
    command('zero')
    assert abs(api('state')['position'][2]) < .05
    command('mode', value=1)
    command('axis', enabled=True)
    command('system', enabled=True)
    command('pwm', value=.125)
    state = wait(lambda s: s['dsp']['duty'][2] == -.125)
    assert state['pwm_target'][2] == .125
    command('stop')
    try:
        api('data/load_csv', {'name':'wrong.csv','overwrite':True,'csv':'axis2_reference_mm\n1\n'})
    except HTTPError as ex:
        assert ex.code == 409
    else:
        raise AssertionError('Accepted a linear reference on a rotary axis')
    api('data/load_csv', {'name':'mixed.csv','overwrite':True,'csv':'axis0_reference_mm,axis2_reference_deg\n2,30\n'})
    fields=['axis0_position_mm','axis2_position_deg','axis2_ctrl_r_deg','axis2_ctrl_y_deg','axis2_output_duty']
    api('data/runner', {'operation':'start','completion':'hold','auto_enable':True,'record':True,'signals':fields})
    state = wait(lambda s: abs(s['position'][0]-2) < .05 and abs(s['position'][2]-30) < .05)
    until = time.monotonic()+4
    while time.monotonic()<until:
        rclpy.spin_once(node, timeout_sec=.1)
        if joints:
            j=dict(zip(joints[-1].name, joints[-1].position))
            if abs(j['axis0']-.002)<.00005 and abs(j['axis2']-math.pi/6)<.002:
                break
    else:
        raise AssertionError('Expected axis0 in metres and axis2 in radians')
    api('data/runner',{'operation':'stop'})
    state = wait(lambda s:not s['recording'])
    with urlopen(url+'recordings/'+state['recording_file']) as response:
        lines=response.read().decode().splitlines()
    assert any('axis2 counts_per_deg=' in l and 'reverse_direction=1' in l for l in lines)
    rows=list(csv.DictReader(l for l in lines if not l.startswith('#')))
    assert rows and all(name in rows[-1] for name in fields)
    assert abs(float(rows[-1]['axis2_position_deg'])-30)<.05
    print('PASS: degree targets, wrong-unit rejection, raw encoder/reference and PWM reversal, reset zero, mixed CSV/recording and SI joint states')
finally:
    command('stop')
    node.destroy_node()
    rclpy.shutdown()
