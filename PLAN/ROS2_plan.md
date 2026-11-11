# System Design and Decisions

Updated: 2026-11-11

Status: active requirements and proposals. Delivery order is in
[the roadmap](IMPLEMENTATION_PLAN.md); implemented behavior is in
[the current-system description](CURRENT_SYSTEM.md).

The latest robot/controller direction is in
[Robot packages and controller profiles](ROBOT_AND_CONTROLLERS.md): the complete
robot section belongs to an external robot package; compiled C++ PID/LQR types
use reusable project YAML profiles and per-axis settings panels. The PID/profile
portion was implemented after DSP acceptance on 2026-11-11; external robot
packages and LQR remain pending. See [profile operation](../projects/README.md).
Controller type changes require the affected axis disabled; parameter tuning
must work online. Saving a shared profile requires explicit reload on other axes.
This supersedes the earlier assumption that robot logic must live inside PCD's
`projects/` tree. The existing project alias remains the current implementation.

## 1. Accepted requirements

| Area | Decision |
|---|---|
| Starting point | Two axes already run stably with CSV input; build on this baseline |
| Stage-2 milestone (completed) | Browser position dragging for both working axes using the original DSP firmware |
| First UI units | Axes 0/1 are rack-and-pinion linear actuators, displayed/controlled in mm; retain degrees for future rotary axes |
| Axis-0/1 range | −50 to +50 mm relative to current host zero |
| Stage-2 zero | Capture startup position; retain across enable/disable and browser reconnects; Reset zero is allowed with that motor disabled |
| Slider release | Hold the last requested position |
| Browser disconnect | Keep the host controller and DSP operating; do not disarm or disconnect hardware because the web UI disconnects |
| Recording | Downloadable CSV; capture available data now and extend it with new firmware |
| CSV input checks | Python backend only, warnings only, minimal effort |
| Data collection | Newest available values assembled as one synchronized frame with one frame timestamp; no separate timestamps per signal |
| Rates | Preserve 1 kHz host/SPI for stage 2; target 2 kHz during stage-3 migration |
| DSP role | Simple low-level motor/encoder/I/O endpoint with basic protection and a small state machine |
| Motor type | Brushed DC now; reserve unsupported brushless/FOC capability for a future firmware/hardware build |
| General I/O | Working DIO/AIO is required in stage 3; direct dock wiring is sufficient for testing |
| Hardware changes | Custom hardware redesign later; not a prerequisite for software/I/O testing |
| UI styling | Incorporate the grid, restrained colors, and engineering tables now; refine in stage 4 |
| Network/access | Trusted local network; no login/authentication layer required for stage 2 |

Browser loss is distinct from host/SPI failure. An explicit stop, host control
failure, or DSP communication timeout still invokes stop behavior. Reconnecting
the browser reads the active target/state; it must not overwrite the target,
reset the controller, or automatically re-arm previously disabled hardware.

## 2. Architecture

```text
Browser (TypeScript)
    ↕ HTTP / WebSocket, decimated status
Python backend / UI bridge
    ↕ ROS commands, configuration, status
Pi C++ control path → current/new SPI → DSP → motors and I/O
    ├─ position/controller calculations and trajectory execution
    └─ preallocated sample queue → non-RT CSV writer

Later application behavior:
system node → robot kinematics when needed → same Pi control path
```

### Responsibilities

| Component | Responsibility |
|---|---|
| DSP | Encoders, motor duty output, I/O, latest telemetry frame, local protection |
| C++ real-time loop | Sole SPI owner, axis feedback control, reference execution, bounded sample capture |
| Host non-RT management | Configuration, command handoff, status, recording/file lifecycle |
| Python backend | Web API, ROS client, simple CSV parsing/checks and warning display |
| System node | Manual/CSV/auto selection and system-specific command permissions |
| Robot node | Kinematics and robot-space to axis-space reference conversion |
| Web UI | Calibrated controls, visualization, status, recording requests/download |

Stage 2 implements only the host path needed for two-axis control and recording.
Separate system/robot processes and robot-specific algorithms are not required
before direct axis control works. Any computation that must execute every
control tick belongs in the C++ real-time path.

The implemented stage-2 stack is ROS 2 Jazzy, a C++ `rclcpp` bridge with one
dedicated real-time thread (stage 2: 1 kHz; v5: 2 kHz), Python/FastAPI, and TypeScript/Vite. It does not
require ros2_control plugins or React for these two direct axis controls.
ROS 2 Jazzy and build dependencies are installed on the existing Ubuntu 24.04.4
PREEMPT_RT Pi. CPU 3 remains the isolated control CPU. See
[operation](STAGE2_OPERATION.md) and [environment](CURRENT_SYSTEM.md#deployment-and-verification).
Stage 2 retained the original DSP firmware; stage 3 migrates both DSP cores and
the host to protocol v5. See [stage-3 evidence](STAGE3_RESULTS.md).

Use the trusted local network directly. The owner has removed the previous
login requirement; do not add authentication, TLS infrastructure, or account
management to stage 2. Preserve control correctness and existing motor stop
behavior independently of network access policy.

## 3. Axis configuration and units

Each axis needs a motor/encoder pairing, rotary/linear type, display unit,
encoder scale, transmission scale, direction, zero convention, permitted travel,
controller gains, and duty limit.

The web UI uses degrees or millimeters. The DSP remains in native counts/duty.
Conversions belong on the Pi. If standard ROS joint interfaces are used, rotary
position uses radians and linear position uses meters; conversion to display
units happens at the UI boundary. Raw counts remain available for diagnostics
and recordings.

For a rotary axis with encoder cycles/revolution `C`, decoder multiplier `Q`,
and motor revolutions per output revolution `G`:

```text
counts_per_output_revolution = C × Q × G
position_deg = direction × (count - zero_count) × 360 / counts_per_output_revolution
```

### Confirmed axes 0 and 1: rack and pinion

Both axes use the owner-confirmed `C=500`, `Q=4`, and `G=91/6`, with a 30-tooth,
module-1 pinion on the gearbox output. Both are linear axes, not rotary joints.

```text
pinion_pitch_diameter_mm = module_mm × teeth = 1 × 30 = 30
travel_per_output_revolution_mm = π × 30 = 94.2477796077
counts_per_output_revolution = 500 × 4 × (91/6) = 30333.3333333333
counts_per_mm = (500 × 4 × (91/6)) / (π × 1 × 30) = 321.8466626969
position_mm = encoder_direction × (count - zero_count) / counts_per_mm
target_count = zero_count + encoder_direction × target_mm × counts_per_mm
```

Use the formula rather than a rounded constant in implementation. One count is
approximately 0.00310707 mm; 50 mm is approximately 16,092.33 counts. The UI
range is **−50 to +50 mm** for each axis. This is the nominal mechanical scale;
physical direction and displacement checks belong to stage-2 verification.
Encoder direction and motor polarity are separate settings.

Incremental counts do not establish absolute machine zero after restart.
The accepted stage-2 convention is to capture each axis's measured position as
0 mm once from valid telemetry at host-controller startup, while disarmed.
Retain that zero across enable/disable and browser reconnects; apply the ±50 mm
range relative to it. A host-controller restart establishes a new startup zero.
Do not reset zero every time an axis is enabled. Homing will provide a machine
reference later.

For manual homing now, disable that motor, move the mechanism manually, then
press **Reset zero**. The host captures the latest encoder count and clears that
axis's target/reference/controller state. The other axis can remain enabled.
This does not reset the DSP's native encoder counter. If manual movement puts
the measured position outside ±50 mm, reset zero before enabling again.

Keep existing Pi controller gains in their current count-based form initially;
convert user targets to counts so changing display units does not silently
change the controller tuning. Initialize the target at measured position when
enabling to avoid a jump. Preserve host duty clamps and error handling.

## 4. Commands and operating modes

The complete system supports manual, CSV playback, and application/auto inputs.
One host arbitration point selects the active source. System disable overrides
per-axis enable; system enable does not bypass axis eligibility or limits.

Stage-2 manual controls update the target during dragging and hold the final
target after release. Browser connectivity does not own motor enable lifetime.
A reconnect restores the current display from host state. Stage 2 assumes one
operator on the trusted LAN; HTTP commands are serialized and the latest accepted
target wins. Browser ownership arbitration is deferred until concurrent operators
are required; do not operate conflicting controls from multiple tabs.

Changing input source must not replay queued commands from the previous source.
CSV contents and playback timing policy are kept simple and resolved when that
path is connected; CSV work is not a prerequisite for the first manual-control
acceptance test.

## 5. CSV input policy

- Parse/check CSV only in the Python backend.
- Display advisory warnings; do not add a CSV-quality approval gate.
- Do not duplicate CSV checks in TypeScript or the C++ real-time loop.
- An unreadable file/no usable samples produces a clear load message, never
  invented trajectory values.
- Pass usable numeric samples to the host outside the real-time loop.
- Existing runtime command bounds, finite-value guards, and motor protections
  still apply to commands from every source. They are not CSV validators.

Do not build a general data-cleaning, trajectory-analysis, or validation framework.

## 6. Recording and telemetry

Stage 2 records data already available: the Pi position target, raw encoder
counts, calibrated target/position, requested duty, DSP-reported output duty,
and the existing DSP frame timestamp. Name requested/transmitted/reported values
clearly. The existing full-duplex protocol does not identify exactly which host
position target produced a returned measurement; no additional alignment system
is required for this milestone.

Use one timestamp per received frame. The host may unwrap the existing 32-bit
DSP timestamp for elapsed time; this does not add per-signal timestamps.
Capture each SPI sample at the host loop rate into preallocated storage.
A non-RT writer writes CSV; the backend serves completed files. Start, stop,
status, and download belong in the first functional UI.

Store the axis names/units, calibration, and controller settings with the
recording so values can be interpreted later. Column/schema details can be
settled during implementation. Report sample drops and disk failures without
blocking motor control. No arbitrary one-minute session cap or binary-format
requirement is carried forward from the older plan.

Stage 3 extends the same approach with the newest encoder, reference, duty,
DIO/AIO, and status values in one synchronized DSP frame. One frame timestamp
is sufficient. This means one consistent snapshot of available values, not a
claim that every peripheral physically samples at the same instant. No
per-channel timestamps, interpolation for alignment, or reconstruction of all
intermediate DSP ticks is required.

## 7. Stage-3 DSP features

Confirmed: controllers stay on the Pi; DSP is the duty/encoder/I/O endpoint.
The [v5 protocol](spi_protocol.md) carries all six motor encoder counts, requested
and applied duty, echoed Pi references, 4 DI / 4 DO / 6 AI / 2 AO, and explicit
state/fault/acknowledgment fields in one latest-value frame.

CPU1 uses three eQEP modules. CPU2 polls the remaining three quadrature pairs
with a dedicated bounded loop and publishes coherent snapshots; record actual
polling gaps and external-stimulus coverage during bring-up. Auxiliary encoders
are deferred. DIO/AIO use free dock pins directly, with raw analog codes until
the reference voltage is confirmed. Startup/disarm/fault clear output commands.

Keep disabled-axis Reset zero now. Future automatic homing runs on the Pi using
DSP digital inputs and local limit protection; neither homing sequencing nor
local limit roles are part of this firmware pass. DSP A/B controllers and FOC
are not implemented. PID/LQR implementations and reusable YAML profiles belong
to the later [host controller/robot package design](ROBOT_AND_CONTROLLERS.md).

## 8. Web UI scope

The current three-tab layout follows [UI_design.md](UI_ref/UI_design.md):

1. **Dashboard:** robot-view and robot-control placeholders, DSP list, side-by-side
   axis cards with their own ruler sliders (position/PWM, enable, measured position/duty, plots, controller), recording.
2. **System:** live Pi resource usage/capacity, RT/non-RT node details, and expandable
   ROS topics streamed at 10 Hz with source message counts/age.
3. **Resources:** axis/motor/encoder/calibration register, transport, and expansion I/O.

The header shows Heart Phantom and keeps software Stop All visible. The icon-only
left rail has hover/focus labels, with browser Settings at the bottom: theme,
plot/stream/status rates, and frontend performance stats. Settings do not alter
the real-time controller or recorder rate. Unavailable firmware capabilities and
GPU metrics remain explicitly unavailable.

PCD infrastructure stays in shared source directories. Robot-specific labels,
calibration, bounds, tuning, and position-controller selection live in
`projects/heart_phantom/`; see the [project guide](../projects/README.md). The user
selected **C++ code/configuration first** for advanced controllers. No coefficient
or block-diagram editor is required for this pass. The initial replacement point
is a per-axis C++ error-to-duty controller; coupled controllers need an explicit
input/parameter contract when their design is known.

Direct PWM on the current firmware is implemented as a host mode that bypasses
PID and sends signed duty. Mode changes require the axis disabled; enabling clears
PWM to zero. Release/disconnect retains duty, and leaving the configured position
range faults/disarms. Controllers remain on the Pi.

The first pass uses the cream grid, bold borders, muted green state, red Stop,
and engineering tables from [the references](UI_ref/UI_ref.md). Final visual details,
actual robot model/kinematics, CSV playback, and remaining intermittent web delivery
lag remain follow-up work.

## 9. Open decisions

- Confirm analog reference switches and complete wired I/O stimulus checks.
- Measure CPU2 encoder paths with simultaneous external edges at the required rate.
- Discuss profile application/sharing, LQR state model and robot source-loss policy
  in [the architecture proposal](ROBOT_AND_CONTROLLERS.md) before implementing it.
- Refine UI details later; preserve the working per-axis sliders and two-card row.
