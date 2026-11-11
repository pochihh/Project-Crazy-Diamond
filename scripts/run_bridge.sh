#!/usr/bin/env bash
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
source /opt/ros/jazzy/setup.bash
source ros2_ws/install/setup.bash
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
# Shared-memory DDS stalled on the bench Pi; use the verified loopback transport.
export FASTDDS_BUILTIN_TRANSPORTS=UDPv4
exec ros2 run bridge bridge_node --ros-args --params-file "projects/${DIAMOND_PROJECT:-heart_phantom}/bridge.yaml" -p "mock:=${DIAMOND_MOCK:-${1:-true}}"
