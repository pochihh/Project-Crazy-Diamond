# Verifying a change

Run from the repository root. These checks can run with the physical bridge and motors offline.

| Layer | Command | What it checks |
|---|---|---|
| Firmware | `python3 scripts/build_dsp.py RAM`, then `python3 scripts/build_dsp.py FLASH` | Compiles and links both CPU images in each configuration. Programming is a separate step. |
| Host packages | `bash scripts/build_ros.sh` on ROS 2 Jazzy | Builds the bridge, message/service definitions, web and data relays, and robot packages. Runs the registered colcon tests. |
| Host C++ | `ctest --test-dir ros2_ws/build/bridge --output-on-failure` after building | Checks protocol/CRC, endpoint guards, calibration, controllers, CSV runner, recording and handoffs. |
| Python | `python3 -m pytest -q ros2_ws/src/{data_tools,ui_bridge}/test robots/phantom_robot/test` after sourcing the built workspace | Checks CSV parsing/preview, web relay, system metrics, waveforms and robot startup. |
| Browser | `npm --prefix web_ui ci`, `npm --prefix web_ui test`, `npm --prefix web_ui run build` | Checks chart rendering and commands, type-checks the UI and bundles its assets, including the controller diagrams. |

## Integration tests

Use a separate ROS domain and HTTP ports when the bench services are running. Start the [mock stack](../ros2_ws/doc/operations.md#mock-development) and verify `mock: true` on its `/api/state` before running an experiment test.

- [Control desk and motion](../tests/integration.py) exercises enable, position commands, Reset zero, status and recording through HTTP. Pass `--url` for the mock instance.
- [ROS experiments](../tests/experiments_ros.py) exercises the Phantom, runner and logger together. [Step Recorder](../tests/step_recorder.py) checks baseline, injection, recovery and matched recording rows.
- [Mixed units](../tests/axis_units.py) uses a dedicated mock configuration with a reversed linear axis 0 and reversed rotary axis 2. Its degree tests need that configuration.

## Bench-only checks

The [dock endpoint test](../tests/hardware_endpoint_test.cpp) is a standalone Pi SPI client. Stop the bridge first and use the [specified test jumpers](../dsp/f28379d/README.md#dock-loopback-tests). This checks pin assignments, acquisition and functional CPU2 counting. Maximum encoder speed on three simultaneous inputs and absolute analog accuracy require external measurements.

The [physical soak test](../tests/realtime_soak.py) can move motors, reset origins and write recordings. Use its `--help` to choose duration, runner source and motion verification for an intentional bench test. Compare DSP faults, missed periods, drops and consecutive recording sample numbers over the run.
