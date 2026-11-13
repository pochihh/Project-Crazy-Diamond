import asyncio
import time
from types import MethodType, SimpleNamespace

import pytest
from phantom_robot.node import Phantom, Request
from phantom_interfaces.srv import Command
from rclpy.serialization import deserialize_message, serialize_message


def test_http_defaults_survive_ros_serialization():
    # Pydantic does not coerce omitted defaults; ROS doubles must be Python floats.
    request=Command.Request(**Request().dict())
    actual=deserialize_message(serialize_message(request),Command.Request)
    assert actual.period_s==2.0 and actual.phase_deg==0.0
    assert actual.pulse_width_ms==5.0 and actual.approach_speed_mm_s==5.0
    assert list(actual.amplitude_mm)==[1.0,1.0] and list(actual.offset_mm)==[0.0,0.0]


def coordinator():
    node=SimpleNamespace(generation=1,received_at=time.monotonic(),start_sample=100,
                         state=SimpleNamespace(connected=True,fault='',sample_count=120,stop_count=7,last_command_id=5))
    node.check_start=MethodType(Phantom.check_start,node)
    return node


@pytest.mark.parametrize('failure', ['robot_stop','external_stop','restart','stale','disconnected','fault'])
def test_preparation_cannot_continue_after_stop_or_feedback_loss(failure):
    node=coordinator()
    if failure=='robot_stop':node.generation+=1
    elif failure=='external_stop':node.state.stop_count+=1
    elif failure=='restart':node.state.sample_count=1
    elif failure=='stale':node.received_at-=2
    elif failure=='disconnected':node.state.connected=False
    elif failure=='fault':node.state.fault='SPI failure'
    with pytest.raises(ValueError,match='cancelled'):node.check_start(1,7)


def test_own_stop_receipt_can_arrive_before_its_state_snapshot():
    node=coordinator()
    async def until(condition):
        assert condition() is None  # Older state still has the pre-Stop counter.
        node.state.last_command_id=6
        node.state.stop_count=8
        return condition()
    node.until=until
    assert asyncio.run(Phantom.applied_state(node,SimpleNamespace(command_id=6),1,8)) is node.state
