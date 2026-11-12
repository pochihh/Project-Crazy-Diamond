#!/usr/bin/env python3
"""ROS-only control test. No HTTP/UI dependency. Physical motion requires --physical."""
import argparse
import time
import rclpy
from interfaces.msg import BridgeState, DspState, CommandResult, PositionCommand, DutyCommand, DigitalCommand, AnalogCommand
from interfaces.srv import Command, DspCommand
from rosidl_runtime_py.convert import message_to_ordereddict

parser = argparse.ArgumentParser()
parser.add_argument('--physical', action='store_true')
parser.add_argument('--direct-only', action='store_true', help='Mock with all position controllers disabled')
args = parser.parse_args()
assert not (args.physical and args.direct_only)
rclpy.init()
node = rclpy.create_node('test_direct_ros_control')
latest = [None]
results = {}
raw = {}
node.create_subscription(BridgeState, '/bridge/state', lambda m: latest.__setitem__(0, m), 1)
node.create_subscription(CommandResult, '/bridge/command_result', lambda m: results.__setitem__(m.command_id, m), 256)
def raw_frame(message):
    raw[message.sequence] = message_to_ordereddict(message)
    while len(raw) > 50:
        del raw[next(iter(raw))]
node.create_subscription(DspState, '/dsp/state', raw_frame, 16)
control = node.create_client(Command, '/bridge/command')
device = node.create_client(DspCommand, '/dsp/command')
target = node.create_publisher(PositionCommand, '/bridge/target', 16)
duty = node.create_publisher(DutyCommand, '/dsp/duty', 16)
digital = node.create_publisher(DigitalCommand, '/dsp/digital_out', 16)
analog = node.create_publisher(AnalogCommand, '/dsp/analog_out', 16)

def wait(predicate, seconds=5):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        rclpy.spin_once(node, timeout_sec=.01)
        if predicate():
            return latest[0]
    raise AssertionError('ROS condition timed out: ' + str(latest[0]))

def call(operation, axis=0, value=0., enabled=False):
    future = control.call_async(Command.Request(operation=operation, axis=axis, value=float(value), enabled=enabled))
    wait(future.done)
    response = future.result()
    assert response.accepted, response.message
    wait(lambda: response.command_id in results and latest[0].last_command_id >= response.command_id)
    result = results.pop(response.command_id)
    assert result.applied, result.message

def hold(seconds, check):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        rclpy.spin_once(node, timeout_sec=.02)
        check(latest[0])

wait(lambda: latest[0] is not None and latest[0].connected and control.service_is_ready())
initial = latest[0]
assert initial.mock != args.physical and not initial.recording and not initial.fault
wait(lambda: all(p.get_subscription_count() for p in (target, duty, digital, analog)))
axis = 5 if args.direct_only else 0
try:
    call('stop')
    if not args.direct_only:
        call('zero', axis);call('mode', axis, 0)
        call('axis', axis, enabled=True);call('system', enabled=True)
        target.publish(PositionCommand(axis=axis, position_mm=.5))
        wait(lambda: abs(latest[0].position_mm[axis]-.5)<.3 and latest[0].target_mm[axis]==.5)
        node.destroy_publisher(target)
        def position_held(s):
            assert s.connected and s.system_enabled and not s.fault and s.target_mm[axis]==.5
        hold(1, position_held)
        call('stop')
    else:
        assert not any(initial.position_configured)
    call('mode', axis, 1);call('limit', axis, .1)
    call('axis', axis, enabled=True);call('system', enabled=True)
    duty.publish(DutyCommand(axis=axis, duty=.015))
    wait(lambda: abs(latest[0].dsp.duty[axis]-.015)<1e-6)
    node.destroy_publisher(duty)
    def duty_held(s):
        assert s.connected and s.system_enabled and not s.fault and abs(s.dsp.duty[axis]-.015)<1e-6
    hold(.3 if args.physical else 2, duty_held)
    call('stop')
    wait(lambda: latest[0].dsp.arm_mask==0 and not any(latest[0].dsp.duty))
    call('system', enabled=True)  # General I/O, no motor armed.
    digital.publish(DigitalCommand(channel=3, value=True))
    analog.publish(AnalogCommand(channel=0, value=1234))
    wait(lambda: latest[0].dsp.digital_out==8 and latest[0].dsp.analog_out[0]==1234)
    assert latest[0].dsp.arm_mask==0
    call('stop')
    wait(lambda: latest[0].dsp.digital_out==0 and list(latest[0].dsp.analog_out)==[0,0])
    wait(lambda: latest[0].dsp.sequence in raw)
    assert raw[latest[0].dsp.sequence] == message_to_ordereddict(latest[0].dsp)
    for name in ('telemetry_errors','dropped_samples') + (('deadline_misses',) if args.physical else ()):
        assert getattr(latest[0], name)==getattr(initial, name), name
    print('PASS: ROS-only target/duty/I/O, publisher-exit hold, explicit stop, identical raw DSP relay', flush=True)
finally:
    call('stop');call('mode', axis, initial.control_mode[axis]);call('limit', axis, initial.duty_limit[axis])
    node.destroy_node();rclpy.shutdown()
