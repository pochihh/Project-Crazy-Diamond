#!/usr/bin/env bash
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
source /opt/ros/jazzy/setup.bash
source ros2_ws/install/setup.bash
# Jazzy's LOCALHOST discovery adds shared memory even with UDPv4 selected.
export ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT
export FASTRTPS_DEFAULT_PROFILES_FILE="$root/config/fastdds-loopback.xml"
export DIAMOND_ROOT="$root"
exec ros2 run ui_bridge ui_bridge
