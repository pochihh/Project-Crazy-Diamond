> Historical snapshot. Current system documentation: [index](../../README.md).

# ROS 2 and Web Interfaces

Updated: 2026-11-10

Status: implemented host/web contract with DSP protocol v5.
[System design](ROS2_plan.md) owns behavior and decisions;
[current system](CURRENT_SYSTEM.md) owns the existing wire protocol.

The August interface list assumed a migrated DSP, DSP-owned homing, binary
recording, and immediate multi-DSP support. It is superseded. Do not implement
unavailable DSP commands just to reproduce that list.

## 1. Stage-2 ownership

| Component | Owns |
|---|---|
| C++ bridge/control process | The one SPI session, position controllers, real-time cycle, sample producer |
| Non-RT host callbacks/worker | Validated command/config handoff, status publication, CSV writing |
| Python UI bridge | HTTP/WebSocket, ROS requests/subscriptions, CSV parsing/warnings, downloads |
| TypeScript frontend | Calibrated position controls, state/plots, enable/stop, recording controls |

Callbacks and writer work execute outside the real-time update loop.
Only the C++ bridge opens SPI. The bench test must not run against the same
device while the ROS bridge owns it.

The C++ `bridge` rclcpp node owns a dedicated real-time thread. It converts
position error to duty using the existing count-based PI/PID behavior. No
ros2_control plugin/controller-manager layer is needed for stage 2. DSP
POSITION/VELOCITY commands do not exist in the current protocol.

## 2. Public data model

Each configured axis has a stable name and DSP/index binding. Stage 2 configures
one DSP and the two working axes; do not require multi-DSP machinery to move them.

| Value | Contract |
|---|---|
| UI position target/state | Calibrated degrees (rotary) or mm (linear) |
| Standard ROS joint position/state, if used | Radians (rotary) or meters (linear) |
| DSP position | Signed native encoder counts |
| Duty | Normalized `[-1, +1]`, explicitly called duty, not torque/effort |
| Enable | Host requested state; current firmware has no explicit enable acknowledgment |
| Timestamp | One existing DSP frame timestamp per sample |
| Missing I/O/fault capabilities | Unavailable, not zero-valued measurements or invented DSP state |

Calibration, motor polarity, controller gains, limits, and pairing are host
configuration. Applying changes that affect motion should occur while disarmed.
Motor/encoder scale conversion must preserve the existing count-based controller
tuning during stage 2.

Axes 0/1 are linear rack-and-pinion actuators. Their UI unit is mm, standard ROS
joint unit is meters, nominal scale is 321.8466626969 counts/mm, and UI range is
−50 to +50 mm. Use the full formula and zero convention in
[system design](ROS2_plan.md); no rotary-joint assumption applies to these axes.
Capture zero from valid telemetry once per host-controller startup while disarmed;
retain it across enable/disable and browser reconnects. A host-controller restart
establishes a new zero. Per-axis `zero` captures a new host offset and resets the
target/reference while that motor is disabled; it leaves DSP counts unchanged.
The first backend is served on the trusted LAN without a login layer.

## 3. Minimal logical operations

The definitions are in `ros2_ws/src/interfaces`. ROS names are absolute:

- `/bridge/state`: `interfaces/msg/BridgeState`, reliable depth 1, 50 Hz. Includes
  both axes, target/reference/position in mm, raw/zero counts, duties, host enables,
  tuning, frame timestamp/sample counter, host faults, timing/drop/recorder state.
  Also carries project/controller names, per-axis scale/travel bounds, control
  mode (`0` position / `1` PWM), and normalized PWM target.
- `/joint_states`: `sensor_msgs/msg/JointState`, 50 Hz, names `axis0`, `axis1`,
  positions in meters. The ROS header uses host publication time; the custom
  state and CSV use the DSP frame timestamp.
- `/bridge/command`: `interfaces/srv/Command`; request `operation`, `axis` (0/1),
  `value`, `enabled`; response `accepted`, `command_id`, `message`. Acceptance
  means queued. Applied result appears as `last_command_id`/`command_error` in
  state. The HTTP API waits for that result.

Supported operations: `target`, `pwm`, `mode`, `axis`, `system`, `stop`, `zero`, `clear` (host and DSP fault, leaves outputs disabled), `kp`, `ki`, `kd`, `limit`, `digital`, `analog`, `record_start`, `record_stop`. Tuning changes
are session-only; permanent startup settings live in `projects/heart_phantom/bridge.yaml`.

HTTP routes on port 8000:

| Route | Meaning |
|---|---|
| `GET /api/state` | Latest state plus `bridge_alive` (received within 1 s) and age |
| `POST /api/command` | Command object using the ROS request fields; 409 rejection, 422 invalid input, 503 unavailable, 504 timeout |
| `WS /ws?hz=50` | State snapshots at the requested 10–50 Hz (default 50); no motor lease |
| `WS /ws/topics?name=/bridge/state` | Latest topic message at 10 Hz, source count/age; subscription lasts only while open |
| `GET /api/project` | Selected project manifest / axis labels |
| `GET /api/recordings` | Session files, byte sizes, active flag |
| `GET /api/recordings/{name}` | Completed CSV download; active file returns 409 |
| `GET /api/system` | ROS nodes/topics, kernel, CPU count/load/per-core usage, memory/disk capacity and usage, optional GPU/temperature |
| `/` | Built web control desk |

The Python adapter adds `ui_instance` (new for each web-process run) and
`ui_sequence` (increasing per received ROS state) to snapshots. These order HTTP
fallback and WebSocket updates so delayed packets cannot overwrite fresh state.
They are web-layer metadata; the ROS/SPI frames and recorder are unchanged.
The browser requests one bounded HTTP snapshot at a time after 150 ms without a
fresh sample. Rendering coalesces packet bursts into the latest state per display
frame; plots retain available frame samples and use DSP time, not arrival spacing.

Only one HTTP command is in flight at a time. Direct ROS callers must consume
state acknowledgments themselves. Rejection/timeout must not be interpreted as
proof that a command was never applied.

| Operation | Request / result | Owner |
|---|---|---|
| Read state | Target, measured position, raw counts, duty, requested enables, connection/loop health | Bridge → backend → UI |
| Set target | Named axis and calibrated position; latest accepted target replaces prior target | Backend → command handoff |
| Set PWM | Normalized signed duty; PWM mode only, constrained by duty limit; bypasses PID/polarity | Backend → bridge |
| Select mode | `value=0` position / `1` PWM; disabled axis only; clears duty and initializes position target | Backend → bridge |
| System enable/disable | Global permission; disable stops both axes | Backend → bridge |
| Axis enable/disable | Selected axis request under system permission | Backend → bridge |
| Stop | Explicit motor disarm; no implied restart | Backend → bridge |
| Reset zero | Current measured count becomes host zero; disabled axis only | Bridge |
| Read/apply tuning | Live complete PID updates; type/duty-limit changes while disabled; reusable YAML profiles | Host management |
| Start/stop recording | Start sample capture; stop capture and finish the file outside RT | Host management/writer |
| Recording status/list/download | Active state, sample/drop/error counts, completed CSV files | Host/backend |
| CSV load (future) | Python parsing and advisory warnings; not exposed in stage 2 | Python only |

Changing the arm mask in the existing firmware clears staged duty and stops/
re-arms the motor outputs. The host must handle that transition deliberately;
do not assume independent axis enable changes are seamless.

The `clear` operation queues a DSP CLEAR and keeps the system disabled until
fault-free telemetry is received. `digital` uses channel 0–3 and integer value
0/1; `analog` uses channel 0/1 and integer DAC code 0–4095. Outputs require
System Enable, independently of motor selection. Stop clears all outputs.

State now includes protocol/rate, DSP state/arm/fault/sequence/ack, six raw encoder
counts and validity, DI/DO bits, six ADC results, two DAC commands, validity masks,
invalid/rejected/watchdog/encoder counters and CPU2 maximum polling gap.
CSV includes the same newest-value I/O/count fields with its one frame timestamp.

## 4. Target and connection lifetime

- Manual targets are the stage-2 command source; HTTP requests are serialized.
- Hold the last position target or PWM duty on slider release. PWM has no position hold.
- Every enable clears PWM to zero; preloaded disabled duty is not replayed.
- PWM exceeding measured travel bounds faults/disarms both axes.
- Browser disconnect does not disarm, disconnect the bridge, or end recording.
- Reconnecting the UI loads active state; it does not send initial defaults.
- System disable, explicit stop, or host/SPI control failure still stops motion.
- Host restart starts disarmed; reconnecting communications does not re-arm.
- The trusted-LAN release assumes one operator. Multiple tabs can read/control
  the same state; latest accepted target wins. No ownership/session arbitration
  is implemented. Add it if concurrent operators become a requirement.

## 5. Real-time and recording boundary

The v5 host/SPI cadence is 2 kHz. Reserve the real-time thread
for bounded command consumption, controller computation, one SPI exchange,
counters, and copying the latest sample into preallocated storage. Command
consumption is bounded to 128 per tick; Stop overrides queued enables. Samples
use a 4,096-slot ring. Overflow increments a visible drop counter; it never
blocks the controller. An armed physical cycle exceeding its 500 µs deadline
latches a host fault and disarms. Mock timing is diagnostic only.

Run ROS callbacks, file/network I/O, CSV parsing, diagnostics formatting, and
disk writing outside that thread. The stage-2 CSV writer shares the non-RT ROS
executor; a sustained disk stall can delay commands/status and fill the sample
queue, but cannot block the control thread. Split out a writer if that becomes
a measured limitation. Use decimated UI state/plots, approximately
50 Hz as the earlier proposal suggested; do not push every raw sample through
the browser.

The writer records every received sample using one frame timestamp plus the
available target, raw/calibrated position, requested/reported duty, and session
metadata. Keep the existing protocol's full-duplex delay visible in naming;
do not fabricate same-tick command acknowledgments. No binary recorder or
per-signal timing system is required.

## 6. Later extensions

Robot packages and compiled controller types / reusable YAML profiles are
[documented for discussion](ROBOT_AND_CONTROLLERS.md). Automatic Pi homing,
local limit protection and multiple DSPs remain separate work. The concrete
current wire contract is [protocol v5](spi_protocol.md).


## Controller profiles (2026-11-11)

`/bridge/controllers` (`interfaces/srv/ControllerCatalog`) lists compiled types,
parameter metadata, saved profiles and invalid-file diagnostics. HTTP exposes
it at `GET /api/controllers`. `BridgeState.controllers` contains each axis's
active profile ID, type and complete parameter snapshot; `controller_revision`
identifies changes, and `reference_speed_mm_s` describes the reference slew.

The existing Command service and `POST /api/command` add:

| Operation | Payload / effect |
|---|---|
| `controller_apply` | `controller_type` plus complete parameter names/values; atomic live update for the same type |
| `profile_load` | `profile` ID; read YAML and explicitly apply to one axis |
| `profile_save` | Current `profile` ID and `controller_revision`; atomically save current active parameters |
| `profile_create` | New `profile` ID and `controller_revision`; create without overwrite and select for the current session |

HTTP uses a `parameters` name/value object; the ROS request uses parallel
`parameter_names` / `parameter_values` arrays. Saving rejects stale revisions or
pending commands. Other axes never reload automatically. The catalog and file
operations run outside the RT thread; commands handed to RT contain fixed-size
values only. Type changes require disable; PID parameter updates do not reset
all controller state. Existing `kp` / `ki` / `kd` commands remain compatible and
now support live tuning; the UI applies all gains together.
