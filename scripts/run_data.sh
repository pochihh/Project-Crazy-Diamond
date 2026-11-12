#!/usr/bin/env bash
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
source /opt/ros/jazzy/setup.bash
source ros2_ws/install/setup.bash
export ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT
export FASTRTPS_DEFAULT_PROFILES_FILE="$root/config/fastdds-loopback.xml"
exec ros2 run data_tools data_server
