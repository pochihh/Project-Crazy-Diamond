# ROS bridge and current DSP

The Pi runs one C++ `bridge` node and one Python `ui_bridge` node. The DSP remains
an I/O endpoint using the existing **v5 firmware and 96-word SPI protocol**.
No DSP firmware, hardware watchdog, boot ID, reset-reason field or reboot command
is added by this cleanup.

## Follow one command

```text
Position topic → latest target → reference generator → PID ─┐
                                                          ├→ output gate → SPI → DSP
Duty topic     → latest duty ──────────────────────────────┘
DIO/AIO topics → latest output values ──────────────────────┘
```

The reference generator, controller, output gate and SPI exchange execute in
**one 2 kHz worker** in the bridge process. Direct duty bypasses the position
controller and reference generator. It is signed normalized DSP duty (-1 to +1),
with no PID motor-sign conversion. Controllers use the latest accepted feedback;
SPI has its ordinary sample/pipeline delay. There is no ROS hop between a
controller calculation and its SPI output.

ROS callbacks run on the ordinary executor. They hand continuous values to a
fixed-size, single-producer/single-consumer latest-value mailbox. Intermediate
updates to the same channel are replaced. Discrete commands use a bounded FIFO.
Arrival numbers order the retained inputs against discrete commands at the next
worker boundary. Stop takes priority over queued enables.

The worker is pinned to Pi CPU 3, SCHED_FIFO 80, with current memory locked. ROS,
HTTP/WebSocket, CSV formatting, profile files and log writes run outside that
worker on CPUs 0–2. Topic delivery and executor scheduling still have latency;
inputs are consumed on a worker cycle after their callback runs.

## Output gate and holding behavior

Each motor channel has a selected mode: `0 = position`, `1 = direct duty`.
System enable permits general I/O; the per-axis selection determines the motor
arm mask. The gate requires a synchronized link, successful RT setup and no host
fault. The DSP independently checks its own arm state, faults and encoders.

- **Publisher silence holds the last accepted value.** There is no topic timeout
  or publisher lease. Closing the browser, stopping a publisher, or restarting
  only `ui_bridge` does not stop motion or recording.
- Stop, disable and faults still remove drive. System disable/Stop also clear
  DO and DAC commands. A mode switch requires that axis to be disabled.
- Enabling an axis clears preloaded duty, starts its reference at measured
  position and resets controller history. Old commands are not replayed from the
  input mailbox after stop/re-enable.
- Wrong-mode, non-finite and out-of-range inputs are rejected. The previous
  accepted value remains. `input_rejections`, `input_topic`, `input_channel` and
  `input_error` in bridge state report rejection; these are **host** diagnostics.
- Travel limits remain active for calibrated axes in both modes. Position mode
  also checks tracking error. The default reference advance is 20 mm/s, not a
  trapezoidal acceleration profile.
- Exactly one source should publish each command stream/channel. Multiple
  publishers have arrival-order semantics; no automatic ownership arbitration
  is implemented.

The six hardware channels can receive direct duty. Only axes 0 and 1 have
position control configured in the bench project. Set `axisN.position_control`
false for direct-only operation; that channel does not load a PID profile or
require calibrated position control. Its `position_configured` flag is false,
and its millimeter values are not meaningful. Encoder counts remain available.
The UI still has the two commissioned axis cards. Other physical motor channels
have not been motion-tested by this cleanup.

## ROS interface

Inputs are reliable, volatile, bounded depth 16. Volatile means historical
commands are not replayed when a new bridge process subscribes. State streams
are reliable depth 1. All names below are absolute, for the current single DSP.

| Name | ROS type | Contract |
|---|---|---|
| `/bridge/target` | `interfaces/msg/PositionCommand` | `axis`, `position_mm`; configured position mode only |
| `/dsp/duty` | `interfaces/msg/DutyCommand` | `axis`, `duty`; direct-duty mode only, channels 0–5 |
| `/dsp/digital_out` | `interfaces/msg/DigitalCommand` | `channel` 0–3, boolean `value`; system enabled |
| `/dsp/analog_out` | `interfaces/msg/AnalogCommand` | `channel` 0–1, raw DAC `value` 0–4095; system enabled |
| `/dsp/state` | `interfaces/msg/DspState` | Latest newly decoded DSP frame, at most 50 Hz; no repeated cached frames during a link outage |
| `/bridge/state` | `interfaces/msg/BridgeState` | Host/control/recording state plus the same snapshot's nested `dsp` message, at 50 Hz |
| `/joint_states` | `sensor_msgs/msg/JointState` | Two configured bench positions in meters, at 50 Hz |
| `/bridge/command` | `interfaces/srv/Command` | Discrete host/controller/recording operations listed below |
| `/dsp/command` | `interfaces/srv/DspCommand` | `DISARM=0`, `CLEAR_FAULT=1`; no reboot operation |
| `/bridge/command_result` | `interfaces/msg/CommandResult` | Each discrete command's actual RT application/rejection, keyed by `command_id` |
| `/bridge/controllers` | `interfaces/srv/ControllerCatalog` | Compiled controller types and project YAML profiles |

`/bridge/command` operations:

| Operation | Relevant request fields | Meaning |
|---|---|---|
| `stop` | none | Disable system/motors and zero output requests |
| `system` | `enabled` | Permit/disable system outputs |
| `axis` | `axis`, `enabled` | Select/deselect motor channel |
| `mode` | `axis`, `value` = 0 or 1 | Change source while disabled |
| `zero` | `axis` | Capture measured counts as zero, calibrated axis disabled |
| `clear` | none | Clear **host** fault while disabled; DSP fault must already be clear |
| `limit` | `axis`, `value` | Set normalized duty limit while disabled |
| `controller_apply` | `axis`, `controller_type`, parameter names/values | Apply a complete parameter set; changing type requires disable |
| `profile_load` | `axis`, `profile` | Explicitly load YAML parameters into that axis |
| `profile_save`, `profile_create` | `axis`, `profile`, `controller_revision` | Save current parameters to existing/new profile |
| `record_start`, `record_stop` | none | Begin/end full-rate CSV collection |
| `kp`, `ki`, `kd` | `axis`, `value` | Existing single-PID-parameter service operations |

Service `accepted=true` means **queued**, not that the DSP applied it. The result
topic confirms application by the host worker; DSP `command_sequence`, `arm_mask`,
`fault` and output fields confirm hardware state. The web adapter waits for the
specific discrete result, rather than inferring success from another command's
state. Continuous inputs have no per-message acknowledgement; observe target,
duty/output state and input-rejection diagnostics.

### Example: control without the UI

On the Pi, from the repository root:

```bash
source /opt/ros/jazzy/setup.bash
source ros2_ws/install/setup.bash
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
export FASTDDS_BUILTIN_TRANSPORTS=UDPv4
ros2 topic echo /dsp/state
```

In another shell with the same environment, a zero-duty setup:

```bash
ros2 service call /bridge/command interfaces/srv/Command '{operation: stop}'
ros2 service call /bridge/command interfaces/srv/Command '{operation: mode, axis: 0, value: 1.0}'
ros2 service call /bridge/command interfaces/srv/Command '{operation: axis, axis: 0, enabled: true}'
ros2 service call /bridge/command interfaces/srv/Command '{operation: system, enabled: true}'
ros2 topic pub --once /dsp/duty interfaces/msg/DutyCommand '{axis: 0, duty: 0.0}'
ros2 service call /dsp/command interfaces/srv/DspCommand '{operation: 0}'
```

A nonzero duty remains applied after `ros2 topic pub --once` exits. For position
control, select mode 0 while disabled, enable, and publish `PositionCommand` on
`/bridge/target`. Observe service results and state before proceeding through a
sequence; publishing a setpoint never implicitly enables an output.

The installed services currently restrict DDS to this Pi. Another laptop can
use the same API once DDS discovery/transport is explicitly configured for the
LAN; the web browser already uses the LAN HTTP/WebSocket server.

## DSP telemetry and UI

`DspState` maps every field of the current `PcdTelemetry` into ROS without unit
conversion. The firmware member `watchdogs` is named `command_timeout_count` in
ROS/UI to distinguish it from the MCU hardware watchdog, which is still disabled.
Positions/references are raw counts, duty is normalized, ADC/DAC are raw codes,
and masks/fault bits retain their firmware meanings. `timestamp_us` is the
original 32-bit DSP counter; it wraps normally. `session` is the existing host
session, not a boot ID.

`BridgeState` separately contains requested enable selections, mode, calibrated
positions, controller targets/references/profiles, host faults, timing counters,
recording status and input rejections. Its top-level `timestamp_us` is the DSP
time unwrapped by the host for recording/plots. `feedback_age_ms` is host-measured
age, not an extra per-sensor timestamp. `connected` means the SPI session is
synchronized; a valid but unsynchronized frame can still appear on `/dsp/state`.

The nested `dsp` and host/controller fields come from one worker snapshot. The
Python node subscribes to `/bridge/state`, serializes it to JSON and adds only
web-delivery metadata (`bridge_alive`, message age and UI sequence/instance).
`/api/state` and `/ws` contain the same structure. `bridge_alive` means the ROS
publisher is alive; it does not by itself mean DSP feedback is fresh.

HTTP `/api/command` maps continuous inputs to the four ROS topics and returns
`published=true` immediately. This is not an application acknowledgement. Mode
and range validation remains in C++; rejection is shown from bridge state.
Discrete commands map to the corresponding ROS services and await their result.
`dsp_clear` and `dsp_disarm` map to `/dsp/command`. The adapter performs no control
calculation, reference generation or enable ownership.

The DSP panel shows reported state, session/acknowledgement, arm/fault masks,
invalid frames, rejected commands, command timeouts, encoder errors/validity and
CPU2 polling gap. Its Disarm and Clear DSP fault buttons have separate meanings.
The host fault banner clears only host faults. Cached DSP data is labeled as
cached during an outage. **Reset zero changes a host coordinate offset; neither
fault clear nor Reset zero reboots the DSP or clears DSP diagnostic counters.**

## Restart and fault behavior

The host examines session identity before comparing DSP timestamps. A zero DSP
session identifies the existing firmware's unsynchronized boot state. It starts
a fresh `SYNC` handshake, establishes new timestamp/sequence baselines only for
that session and remains disarmed. Stale frames within an established session
are still rejected. A failed SPI-device open is retried once per second.

- Browser/UI restart: preserve control and recording.
- Bridge/Pi restart: start disarmed, establish a fresh session, capture startup
  zeros. DSP command timeout stops outputs if the old host disappears abruptly.
- DSP restart: reconnect automatically, latch a host communication fault, capture
  a new startup zero after synchronization and end the current CSV segment.
  Re-enable only after explicit fault clear. Recording requires Start again.
- Communication interruption without a DSP restart: resynchronize disarmed;
  preserve the existing encoder origin. An existing DSP fault is not silently
  cleared by synchronization.

The DSP's command timeout is still 50 ms and is enforced in CPU1's 5 kHz ISR.
CPU2 encoder availability expires after 10 ms without fresh IPC data. Hardware
watchdog changes and reset-cause reporting are deferred. A DSP reboot command
cannot be offered without changing firmware or adding a reset wire.

## Recording, logs and operation

CSV retains the two configured axes' controller/position fields and all six raw
encoder channels plus existing DIO/AIO fields, from the 2 kHz sample queue.
Formatting and disk writes remain outside RT. An interrupted DSP time origin
ends the CSV rather than mixing different origins in one file.

`recordings/realtime-events.jsonl` and the service journal record timing faults.
Every overrun and number of missed periods is retained. Identical continuous
SPI errors are summarized at most once per second with `repeated_errors` and
cumulative `telemetry_errors`; the first/change/recovery event retains context.
Diagnostic queue overflows are counted and reported. A link outage must not
produce thousands of duplicate full-frame log entries per second.

The existing `diamond-bridge` and `diamond-ui` systemd services remain separate.
Both run from `~/Project-Crazy-Diamond`; Git is the source synchronization method.
See [deployment](../PLAN/GIT_WORKFLOW.md), [DSP functions/pins/build/debug](../dsp/f28379d/README.md)
and [v5 protocol](../PLAN/spi_protocol.md).

## Source map and checks

| File | Responsibility |
|---|---|
| `../dsp/host/spi.hpp` | Exclusive SPI ownership, wire codec use, session/freshness checks |
| `src/bridge/include/bridge/control.hpp` | Enable/mode/fault gate, calibration and controller execution |
| `src/bridge/include/bridge/inputs.hpp`, `latest.hpp` | Latest input handoff, no controller logic |
| `src/bridge/include/bridge/controllers.hpp` | Compiled controller registry and implementations |
| `src/bridge/src/bridge_node.cpp` | ROS endpoints, worker orchestration, snapshot publication and non-RT recording |
| `src/interfaces/` | Public ROS message/service definitions |
| `src/ui_bridge/ui_bridge/app.py` | HTTP/WebSocket ↔ ROS adapter |
| `../web_ui/src/` | Display and operator input |

`bash scripts/build_ros.sh` builds and tests all ROS packages on the Pi.
`npm --prefix web_ui test` and `npm --prefix web_ui run build` validate the UI.
`tests/ros_topics_integration.py` exercises control without HTTP; use mock mode
unless intentionally passing `--physical`. The existing HTTP/controller/I/O
integration tests cover the UI adapter and recorder. Physical reset/hang tests
must be distinguished from host state-machine unit tests in acceptance records.
