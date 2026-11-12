#!/usr/bin/env bash
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source /opt/ros/jazzy/setup.bash
cd "$root/ros2_ws"
colcon build --base-paths src ../robots --symlink-install --executor sequential --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDIAMOND_PROJECT="${DIAMOND_PROJECT:-heart_phantom}"
colcon test --base-paths src ../robots --executor sequential
colcon test-result --verbose
