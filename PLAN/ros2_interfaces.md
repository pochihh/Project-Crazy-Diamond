# ROS 2 Architecture and Interfaces

Status: first-pass ROS 2 Jazzy contract.

Last updated: 2026-08-05

This is the authoritative ROS 2 design, including the complete first-pass
application topic and service inventory. It supports one physical DSP now and
multiple configured/mock DSPs from the first release.

## 1. Runtime architecture

```text
web browser
    │ HTTP/WebSocket (LAN only)
    ▼
/ui_bridge ──► /robot ── public units ──► ros2_control /bridge
    │                                         │
    └──── services/status ─► /bridge_manager  ├─ 1 kHz SPI baseline ─► DSP 0 (5 kHz control)
                                               └─ mock SPI ──────────► DSP 1…n
```

| Node/process | Package | Responsibility |
|---|---|---|
| `/bridge` | `bridge` | Renamed `ros2_control_node`; owns controller manager, hardware components, and the 1 kHz host/SPI real-time loop |
| `/bridge_manager` | `bridge` | Non-real-time endpoint registry, configuration, diagnostics, and recording |
| `/robot` | `robot` | Non-RT supervisory/manual command arbitration in geared-output units; never packs SPI |
| `/ui_bridge` | `ui_bridge` | Authenticated LAN HTTP/WebSocket API and ROS client |
| `/robot_state_publisher` | standard package | Publishes the robot model and TF when a URDF is loaded |

Each configured DSP has a stable lowercase `dsp_id` and a ros2_control hardware
component. Topics and services use `/dsp/<dsp_id>/...`. Joint names must be
globally unique; the default convention is `<dsp_id>_<axis_name>`.

Only `/robot` publishes accepted live commands to a DSP command topic. The UI
and other manual clients send requests to `/robot/manual_command`; this keeps
arbitration in one place and prevents competing publishers from directly
driving hardware.

Any future 1 kHz LQR or model-based controller runs as a C++ ros2_control
controller plugin inside `/bridge` on CPU 3, not through a DDS topic. The plugin
may live in the `robot` package; a separate controller package is unnecessary
until there is more than one reusable controller library.

## 2. Units and ros2_control interfaces

The public ROS/UI unit contract is:

| Interface | Unit |
|---|---|
| `position` | geared-output radians by default |
| `velocity` | geared-output radians/second by default |
| `duty_cycle` | normalized `[-1.0, +1.0]` |

Each joint exports state and command interfaces named `position`, `velocity`,
and `duty_cycle`. Exactly one command interface may be claimed per joint.
ros2_control mode-switch hooks map the claimed interface to the DSP's
`POSITION`, `VELOCITY`, or `DUTY` mode and reset its controller state safely.

Each DSP may also expose four named auxiliary encoders with `position` and
`velocity` state only. They have independently configured public units and no
ros2_control command interface, motor limits, or arm state.

The `bridge` hardware plugin performs the final configured conversion between
public output units and DSP-native counts/counts-per-second. Raw cyclic values
remain available only in diagnostics and recordings; they are not another 1 kHz
DDS command path.

## 3. Topic inventory

This table lists every project-owned topic and every standard topic explicitly
consumed by the first-pass application. ROS-generated plumbing such as
`/rosout`, `/parameter_events`, and controller-manager introspection topics is
not an application interface.

### 3.1 Global topics

| Topic | Type | Publisher → subscribers | Rate / QoS | Purpose |
|---|---|---|---|---|
| `/robot_description` | `std_msgs/msg/String` | `/robot_state_publisher` → `/bridge` | Transient-local, reliable, keep last 1 | URDF used by controller manager |
| `/joint_states` | `sensor_msgs/msg/JointState` | joint-state broadcaster → `/robot_state_publisher`, `/ui_bridge`, tools | 50 Hz; sensor-data QoS | Standard position/velocity state for all joints |
| `/dynamic_joint_states` | `control_msgs/msg/DynamicJointState` | joint-state broadcaster → diagnostic/tools | 50 Hz; sensor-data QoS | Includes nonstandard `duty_cycle` state interfaces |
| `/tf` | `tf2_msgs/msg/TFMessage` | `/robot_state_publisher` → ROS consumers | Standard TF QoS | Moving transforms when a robot model is configured |
| `/tf_static` | `tf2_msgs/msg/TFMessage` | `/robot_state_publisher` → ROS consumers | Transient-local standard TF QoS | Fixed transforms |
| `/diagnostics` | `diagnostic_msgs/msg/DiagnosticArray` | `/bridge`, `/bridge_manager` → `/ui_bridge`, diagnostic tools | 1 Hz and on state change; reliable, keep last 10 | RT timing, SPI, DSP, config, and recorder health |
| `/dsp_registry` | `interfaces/msg/DspRegistry` | `/bridge_manager` → `/ui_bridge`, tools | 1 Hz and on change; transient-local, reliable, keep last 1 | Configured endpoints and connection states |
| `/recording/status` | `interfaces/msg/RecordingStatus` | `/bridge_manager` → `/ui_bridge` | 2 Hz while active and on change; transient-local, reliable, keep last 1 | Active session, devices, duration, dropped records, writer errors |
| `/robot/manual_command` | `interfaces/msg/AxisCommandArray` | `/ui_bridge` or one authorized test client → `/robot` | Up to 50 Hz; reliable, keep last 1 | Bounded manual command request before arbitration |

If no robot model is loaded, `/robot_description`, `/tf`, and `/tf_static` are
absent; this does not prevent motor bench testing.

### 3.2 Per-DSP topics

Substitute the configured ID for `<id>`.

| Topic | Type | Publisher → subscribers | Rate / QoS | Purpose |
|---|---|---|---|---|
| `/dsp/<id>/command` | `interfaces/msg/AxisCommandArray` | `/robot` → ros2_control command controller | Up to 100 Hz; reliable, keep last 1 | Six public-unit commands and requested modes |
| `/dsp/<id>/axis_state` | `interfaces/msg/AxisStateArray` | `/bridge_manager` → `/ui_bridge`, tools | 50 Hz; best-effort, keep last 1 | UI-ready position, velocity, duty, mode, enable, home-switch, and per-axis fault state |
| `/dsp/<id>/aux_encoder_state` | `interfaces/msg/EncoderStateArray` | `/bridge_manager` → `/ui_bridge`, tools | 50 Hz; best-effort, keep last 1 | Four configurable encoder-only sensor positions, velocities, and raw counts/rates |
| `/dsp/<id>/status` | `interfaces/msg/DspStatus` | `/bridge_manager` → `/ui_bridge`, `/robot` | 1 Hz and on change; transient-local, reliable, keep last 1 | Identity, compatibility, safety state, config revision, last-seen, and communication counters |

The ros2_control and SPI update loop remains 1 kHz in the baseline while each
DSP snapshots and controls at 5 kHz. Publishing 1 kHz DDS telemetry is
intentionally omitted; recordings receive every SPI sample from the in-process
real-time ring instead.

## 4. Application message contracts

Motor-axis arrays are bounded to six and auxiliary-encoder arrays to four; all
parallel arrays must have matching lengths. Receivers reject unknown `dsp_id`,
duplicate names, non-finite values, and invalid modes.

### `AxisCommandArray`

- `std_msgs/Header header`
- `string dsp_id`
- `string[<=6] joint_names`
- `uint8[<=6] modes` (`0=DISABLED`, `1=POSITION`, `2=VELOCITY`, `3=DUTY`)
- `float64[<=6] commands` in the public units defined above

### `AxisStateArray`

- `std_msgs/Header header`
- `string dsp_id`
- `string[<=6] joint_names`
- `float64[<=6] position`
- `float64[<=6] velocity`
- `float32[<=6] duty_cycle`
- `float32[<=6] current` (zero while current feedback is absent)
- `bool[<=6] current_valid`
- `bool[<=6] home_active`
- `uint8[<=6] modes`
- `bool[<=6] enabled`
- `uint32[<=6] fault_bits`

### `EncoderStateArray`

- `std_msgs/Header header`
- `string dsp_id`
- `string[<=4] sensor_names`
- `bool[<=4] enabled`
- `int32[<=4] raw_position_counts`
- `float32[<=4] raw_count_rates`
- `float64[<=4] position` in each sensor's configured public unit
- `float64[<=4] velocity` in that unit per second

### `DspStatus`

- configured ID/display name and reported 32-bit device ID;
- endpoint state: `OFFLINE`, `ONLINE`, `INCOMPATIBLE`, or `ACTIVE`;
- firmware/protocol versions, motor-axis count, and auxiliary-encoder count;
- DSP safety state, active configuration revision, and fault bitmap;
- host monotonic last-seen time, command/telemetry sequences, CRC errors,
  stale frames, timeouts, and deadline misses.

### `DspRegistry`

- ROS timestamp;
- bounded array of `DspStatus` entries, one per configured endpoint.

### `RecordingStatus`

- state: `IDLE`, `STARTING`, `RECORDING`, `STOPPING`, or `ERROR`;
- session ID, selected DSP IDs, monotonic start time, elapsed duration;
- records written/dropped and the last writer error.

## 5. Service inventory

Services are non-real-time. Their callbacks validate requests and pass bounded
state to the RT loop through `realtime_tools::RealtimeBuffer` or the existing
ros2_control equivalent.

| Service | Type | Owner | Contract |
|---|---|---|---|
| `/bridge_manager/probe` | `interfaces/srv/ProbeDsps` | `/bridge_manager` | Probe only endpoints declared in `system.yaml`; return the registry snapshot |
| `/bridge_manager/reload_configuration` | `std_srvs/srv/Trigger` | `/bridge_manager` | Validate the canonical YAML and stage changed inactive endpoints |
| `/bridge_manager/apply_configuration` | `interfaces/srv/ApplyConfiguration` | `/bridge_manager` | Validate a structured config, apply raw limits/A/B arrays and auxiliary decoder settings while disarmed, await the DSP revision, then atomically persist YAML |
| `/dsp/<id>/activate` | `std_srvs/srv/SetBool` | `/bridge_manager` | Connect/disconnect one configured endpoint; activation requires compatible identity/protocol |
| `/dsp/<id>/arm` | `std_srvs/srv/Trigger` | `/bridge_manager` | Arm only after config revision, limits, and software/decoder/control checks pass |
| `/dsp/<id>/disarm` | `std_srvs/srv/Trigger` | `/bridge_manager` | Request immediate protocol disarm; remains available whenever connected |
| `/dsp/<id>/software_estop` | `std_srvs/srv/Trigger` | `/bridge_manager` | Latch PWM zero, all STBY low, and shared EN low; never substitutes for the physical E-stop |
| `/dsp/<id>/clear_fault` | `interfaces/srv/ClearFault` | `/bridge_manager` | Clear selected clearable faults only when the physical condition is gone |
| `/dsp/<id>/zero_encoders` | `interfaces/srv/EncoderMask` | `/bridge_manager` | Zero selected motor-axis and/or auxiliary encoder counts while disarmed |
| `/recording/start` | `interfaces/srv/StartRecording` | `/bridge_manager` | Validate selected IDs, create a session, snapshot config, then enable capture |
| `/recording/stop` | `std_srvs/srv/Trigger` | `/bridge_manager` | Disable capture, drain and fsync the writer, then return the session ID |
| `/recording/list` | `interfaces/srv/ListRecordings` | `/bridge_manager` | Return metadata/download IDs for completed local sessions |

Standard `/controller_manager/*` services remain available to ROS tools for
loading, switching, and inspecting controllers. They are not exposed directly
by the web UI.

### 5.1 Homing action

| Action | Type | Owner | Contract |
|---|---|---|---|
| `/dsp/<id>/home_axis` | `interfaces/action/HomeAxis` | `/bridge_manager` | Home one axis using the DSP sequence; feedback reports phase/raw count/switch state; cancel sends `ABORT_HOMING` and disarms |

`HomeAxis` goal contains `dsp_id` and axis index. Result contains success,
result code, and latched zero count. The action is rejected unless the DSP is
connected, disarmed, configured, and no other axis is homing.

## 6. Real-time boundary

The CPU 3 controller-manager update thread, at the first-pass baseline:

- runs at a 1 kHz target under documented `SCHED_FIFO` priority;
- calls `mlockall`, pre-faults its stack, and allocates buffers before start;
- performs one fixed 160-byte full-duplex SPI transaction per active physical
  DSP per cycle;
- runs active controllers and updates fixed counters;
- performs no heap allocation, filesystem/network I/O, ROS logging, parameter
  calls, or service work;
- transfers telemetry to non-RT consumers with preallocated single-producer,
  single-consumer storage.

The initial physical setup activates one DSP. Multi-DSP timing must still be
measured before more physical devices are enabled in the same 1 kHz process.
Non-RT executors, the UI, IRQs, and recording writer run on CPUs 0–2.

The Pi is the SPI master, so `/bridge` schedules each transaction and therefore
chooses the stream rate. The v4 frame does not change for a later one-DSP 5 kHz
stream, but that mode is enabled only after the host loop, Linux scheduling,
SPI transaction, DSP DMA re-arm, and 200 µs end-to-end deadline pass the HIL test
in [spi_protocol.md](spi_protocol.md). The DSP control loop remains 5 kHz at
either stream rate.

Host baseline: Ubuntu 24.04, ROS 2 Jazzy, a verified PREEMPT_RT kernel, isolated
CPU 3, fixed test governor, SPI IRQ kept off CPU 3 unless measurement says
otherwise, and a recorded `cyclictest` result before ROS integration.

## 7. Configuration flow

`config/system.yaml` is the canonical source. The Pi validates all fields,
computes encoder/gear conversions and raw DSP limits, stages one-axis raw limits
and the position/velocity A/B coefficient arrays plus auxiliary decoder settings,
commits the complete revision, checks acknowledgements, and confirms the active
revision before allowing arm.
A failed apply leaves the prior DSP revision and YAML file
active. A successful UI edit is written via temporary file, `fsync`, and atomic
rename.

Hardware pin routing, decoder type, and PWM slew are reviewed board-profile data,
not free-form web edits. Encoder cycles, gear ratio, sign, zero, public units, limits,
auxiliary enable/rate limits, and controller coefficients (orders 0–16) are
editable while disarmed. The multiplier is fixed by hardware path (`×4` for all
six motor axes and `×2` for auxiliary encoders), not freely editable.

## 8. Recording and web UI

Recording begins only after `/recording/start` and is intended for sessions under
one minute. The RT loop copies the host monotonic timestamp, DSP ID, raw command
frame, and raw telemetry frame into a preallocated ring. A non-RT writer owns all
file operations. Disk-full or writer failure stops recording and reports an error
without blocking motor control. Binary is canonical; CSV conversion is deferred
until an analysis consumer needs it. “Full rate” means every SPI transaction:
1 kHz at the baseline, or 5 kHz if that later one-DSP stream mode is enabled.

The UI connects only to `/ui_bridge` on the trusted LAN. It receives approximately
50 Hz plot/state updates, while status and diagnostics use the rates above. It
provides login, registry/probe/activate, configuration apply/revert, bounded
manual commands, arm/disarm/software-E-stop/clear/zero/home, plots, recording, download, and diagnostic
views, including the four encoder-only sensor channels. A browser disconnect is
not a safety mechanism; the DSP watchdog is.

## 9. Repository layout

Folder and package names do not use a `crazy_diamond_` prefix.

```text
config/
  system.yaml
PLAN/
  electronics.md
  wiring_table.md
  spi_protocol.md
  ros2_interfaces.md
  plan.md
  IMPLEMENTATION_PLAN.md
  DSP_DEV_HISTORY.md
docker/
dsp/
  f28379d/
    cpu1/                  # SPI, safety, snapshots, CLA/control integration
    cpu2/                  # three ×4 eCAP and four ×2 XINT encoder decoders
    shared/                # fixed IPC structs and protocol definitions
ros2_ws/
  src/
    interfaces/            # messages and services above
    bridge/                # SPI/mock sessions, ros2_control, registry, recorder
    robot/                 # command arbitration and high-level public-unit control
    ui_bridge/             # FastAPI/WebSocket backend and ROS client
    bringup/               # launch files and hardware/mock profiles
web_ui/                    # React frontend; not a ROS package
```

No transport factory, dedicated recorder package, or separate controller package
is introduced until a second real use requires one.

## 10. Required multi-DSP tests

- Registry behavior with zero, one, and two configured mock endpoints.
- One endpoint going offline and reconnecting without disturbing another.
- Duplicate configured IDs and duplicate reported device IDs rejected.
- Protocol-version mismatch shown as `INCOMPATIBLE` and never activated.
- Independent command/telemetry sequences with no cross-device command leakage.
- Commands on `/dsp/a/command` never reach `/dsp/b`.
- Auxiliary encoder state remains namespaced and never creates a command interface.
- Configuration revisions and service results remain per-device.
- One recording containing selected devices preserves the correct DSP ID on
  every sample.
- UI selection changes subscriptions/views without reconnecting directly to DSPs.
