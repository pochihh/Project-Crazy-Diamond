"""Management-only ROS requests with bounded response/application waits."""
import time
from rclpy.node import Node
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.task import Future
from interfaces.msg import BridgeState, CommandResult


class Operations(Node):
    def __init__(self, name):
        super().__init__(name)
        self.group = ReentrantCallbackGroup()
        self.state = None
        self.results = {}
        self.waits = []
        self.create_subscription(BridgeState, '/bridge/state', self.update, 1, callback_group=self.group)
        self.create_subscription(CommandResult, '/bridge/command_result', self.result, 256, callback_group=self.group)
        self.create_timer(.05, self.poll, callback_group=self.group)

    def update(self, state):
        self.state = state

    def result(self, result):
        self.results[result.command_id] = result
        if len(self.results) > 512:
            del self.results[next(iter(self.results))]

    def poll(self):
        remaining = []
        for future, deadline, condition in self.waits:
            if future.done():
                continue
            try:
                value = condition()
                if value is not None:
                    future.set_result(value)
                elif time.monotonic() >= deadline:
                    future.set_exception(TimeoutError('ROS operation timed out; inspect live state before retrying'))
                else:
                    remaining.append((future, deadline, condition))
            except Exception as error:
                future.set_exception(error)
        self.waits = remaining

    async def until(self, condition, seconds=10):
        future = Future()
        self.waits.append((future, time.monotonic()+seconds, condition))
        return await future

    async def request(self, client, request, applied=True):
        if not client.service_is_ready():
            raise RuntimeError(f'Service unavailable: {client.srv_name}')
        pending = client.call_async(request)
        result = await self.until(lambda: pending.result() if pending.done() else None)
        if not result.accepted:
            raise RuntimeError(result.message)
        if applied and getattr(result, 'command_id', 0):
            ack = await self.until(lambda: self.results.get(result.command_id))
            if not ack.applied:
                raise RuntimeError(ack.message)
        return result
