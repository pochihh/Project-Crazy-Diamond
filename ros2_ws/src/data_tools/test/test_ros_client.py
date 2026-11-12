from types import SimpleNamespace
from data_tools.ros_client import Operations


def test_bridge_restart_discards_old_command_acknowledgements():
    node = SimpleNamespace(state=SimpleNamespace(sample_count=100), results={1: 'old'})
    Operations.update(node, SimpleNamespace(sample_count=101))
    assert node.results
    Operations.update(node, SimpleNamespace(sample_count=1))
    assert not node.results
