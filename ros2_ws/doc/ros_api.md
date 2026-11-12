# ROS interface

Names are absolute for the current single-DSP deployment. Generic message/service
definitions live in `ros2_ws/src/interfaces/`; phantom-specific definitions live
in `robots/phantom_interfaces/`. Callers do not need the web UI.

## Motion and hardware

| Endpoint | Type | Meaning |
|---|---|---|
| `/bridge/target` | `interfaces/msg/PositionCommand` | `axis`, `position_mm`; position mode |
| `/dsp/duty` | `interfaces/msg/DutyCommand` | `axis`, `duty`; direct mode, signed normalized DSP duty |
| `/dsp/digital_out` | `interfaces/msg/DigitalCommand` | `channel`, boolean `value` |
| `/dsp/analog_out` | `interfaces/msg/AnalogCommand` | `channel`, uint16 `value` 0…4095 |
| `/dsp/state` | `interfaces/msg/DspState` | Latest newly decoded raw frame, maximum 50 Hz |
| `/bridge/state` | `interfaces/msg/BridgeState` | Host/control/recorder/runner state and nested DSP snapshot, 50 Hz |
| `/joint_states` | `sensor_msgs/msg/JointState` | Two calibrated bench positions in meters |
| `/bridge/command` | `interfaces/srv/Command` | Discrete enable/mode/configuration operations |
| `/dsp/command` | `interfaces/srv/DspCommand` | DISARM=0 or CLEAR_FAULT=1; no reboot operation |
| `/bridge/command_result` | `interfaces/msg/CommandResult` | Actual worker application result by command ID |
| `/bridge/controllers` | `interfaces/srv/ControllerCatalog` | Compiled descriptors and saved profiles |

Input topics are reliable, volatile depth 16, then coalesced per channel. State
streams are reliable depth 1. `/data/preview` is best-effort depth 1. Discrete
`accepted=true` means queued; wait for the matching CommandResult before treating
it as applied. A hardware acknowledgement is separately exposed in DSP state.
Continuous inputs have no per-message ack; observe state and `input_rejections`.
Position references are not restricted by `minimum_mm`/`maximum_mm`; those fields
describe the UI slider range. Finite values must fit the DSP's signed 32-bit
encoder-count reference field. PWM duty bounds and SPI/DSP fault gates remain.

`Command.operation` supports `stop`, `system`, `axis`, `mode`, `zero`, `clear`,
`limit`, `controller_apply`, `profile_load`, `profile_save`, `profile_create`, and
legacy `kp`/`ki`/`kd`, `record_start`/`record_stop`. `mode` uses value 0=position,
1=duty. `clear` is host-only; clear any DSP fault first. Type/reference-mode and
hardware limit changes require disable; PID parameter tuning is online.

`controller_apply` takes complete `parameter_names`/`parameter_values` and a
compiled `controller_type`. Set `set_reference=true` to apply `slew_enabled` and
`slew_rate_mm_s`; otherwise existing reference configuration is preserved. Save
operations require the current `controller_revision` to avoid saving stale state.

For controller/profile/gain/duty-limit commands, `CommandResult.has_controller`
is true and its `axis`, `controller_revision`, `controller` and `duty_limit` fields
are read from the RT controller immediately after that command (also on rejection).
These fields describe that exact command boundary even if another node changes
the axis again before the result arrives. `/bridge/state.controllers` describes
the latest active settings. The HTTP command response includes
`controller_readback` and verifies explicitly requested values before returning
success; a missing or mismatched read-back is an error, not a successful Apply.

## Logger and runner

| Service | Type | Request / response |
|---|---|---|
| `/data/signals` | `interfaces/srv/SignalCatalog` | Empty request; signal names/units/groups/numeric flags |
| `/data/logger` | `interfaces/srv/LoggerCommand` | `operation=start/stop`, `signals[]` |
| `/data/programs` | `interfaces/srv/ListPrograms` | Empty request; persistent filenames and byte sizes |
| `/data/load_csv` | `interfaces/srv/LoadCsv` | `name`; optional `csv` upload text and explicit `overwrite`; warnings/rows/channels returned |
| `/data/program` | `interfaces/srv/LoadProgram` | Typed preloaded program: `name`, `channels[]`, row-major `values[]` |
| `/data/runner` | `interfaces/srv/RunnerCommand` | `operation=start/stop`, `completion=hold/stop/loop`, `auto_enable`, `record`, `signals[]` |

`LoadProgram` channel IDs: 0–5 reference mm, 6–11 duty for axes 0–5,
12–15 DO0–3, 16–17 AO0–1 raw codes. Array width is `channels.size()`; values must
contain complete rows. This management API allows robot nodes to prepare typed
programs without CSV. It does not stream one ROS message per realtime tick.

For multi-step coordinators, set `check_stop_count=true` and pass the
`expected_stop_count` observed before preparing the experiment. The worker
rejects Start if an explicit Stop has occurred since then, so a pending load
cannot re-enable outputs after Stop All. This is a host command-order counter.

Runner state in BridgeState: 0=loaded/idle, 1=running, 2=holding final values,
3=complete/stopped, 4=interrupted. `runner_row` is zero-based; `runner_cycles`
counts completed traversals. Program ID/name/row count, completion mode and owned
channel masks are also published. Recorder state includes filename, active signal
names, sample count and error. See [data semantics](data.md).

```bash
source /opt/ros/jazzy/setup.bash
source ros2_ws/install/setup.bash
export ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT
export FASTRTPS_DEFAULT_PROFILES_FILE="$PWD/config/fastdds-loopback.xml"
ros2 service call /data/load_csv interfaces/srv/LoadCsv '{name: experiment.csv}'
ros2 service call /data/runner interfaces/srv/RunnerCommand \
  '{operation: start, completion: loop, auto_enable: true, record: true, signals: [axis0_position_mm, axis0_output_duty, do0]}'
ros2 topic echo /bridge/state
ros2 service call /data/runner interfaces/srv/RunnerCommand '{operation: stop}'
```

Wait for applied results/state when sequencing calls programmatically. The examples
assume the corresponding CSV is already in the library and the bridge is ready.

## UI relay and robot panel

`GET /api/state` and `/ws` relay BridgeState with UI freshness/sequence metadata
and the recent robot manifest. `/api/command` maps continuous controls to topics
and discrete operations to services. `/api/data/signals`, `/api/data/programs`,
`POST /api/data/logger`, `/api/data/runner`, `/api/data/load_csv` relay the services
above. `/ws/data` carries bounded batches of preview frames. `/api/recordings`
lists files; `/api/recordings/<name>` downloads a closed CSV.

`/robot/panel` (`interfaces/msg/RobotPanel`) advertises API version 1, stable ID,
name and an HTTP(S) panel URL. The UI hides the robot section after two seconds
without a manifest heartbeat. This does not cancel a running experiment.
The phantom API is `/phantom/command` (`phantom_interfaces/srv/Command`), documented
in [robot packages](robot_packages.md).
