# Project Crazy Diamond — Implementation Plan

Status: ready for M0 validation; energized multi-axis work is blocked until the
pin, all-channel level-shifter, CPU2 encoder-rate, and independent-stop gates in
[`electronics.md`](electronics.md) pass.

Last updated: 2026-08-05

This is the execution plan for the project described in [`plan.md`](plan.md). The
original file remains the project brief. This file records the decisions, interfaces,
order of work, and acceptance gates.

Authoritative interface references:

- [Electronics and DSP pin assignment](electronics.md)
- [TMDSHSECDOCK point-to-point wiring table](wiring_table.md)
- [SPI DMA protocol v4](spi_protocol.md)
- [ROS 2 architecture, topics, and services](ros2_interfaces.md)

## 1. First-pass outcome

The first pass will deliver:

- One Raspberry Pi 5 running a PREEMPT_RT Linux kernel and ROS 2 Jazzy.
- One physical TMS320F28379D DSP connected over 10 MHz SPI.
- Six brushed DC motor axes using the current PCB's `PWM/DIR/STBY` per-axis
  signals and one shared `EN`. The reference hardware uses MC33926 H-bridges, but the
  control and host configuration must not hardcode one motor model.
- Six A/B incremental encoders, with optional index. The reference sensor is
  Maxon 110514 (HEDL-5540, 500 cycles/turn, RS-422 line driver) on a Maxon
  148867 motor and Maxon 203116 gearhead with absolute ratio `91/6`.
- Four additional configurable A/B encoder-only sensors, decoded by CPU2 and
  reported without motor command interfaces.
- Per-axis `POSITION`, `VELOCITY`, and normalized `DUTY` control modes.
- Runtime configuration of encoder scale, gearbox ratio, axis direction, limits,
  and bounded CLA transfer-function coefficients from both a versioned config
  file and the web UI.
- A 5 kHz DSP snapshot/control loop plus a C++ ROS 2/SPI path running at a 1 kHz
  target rate on isolated CPU 3.
- A LAN-only web UI with login, DSP status, configuration, commands, 50 Hz plots,
  and explicit recording controls.
- On-demand recordings shorter than one minute, written locally by a non-real-time
  thread and downloadable from the UI.
- Multi-DSP-capable ROS/UI data models and automated tests using mock DSPs. Only
  one physical DSP is required in this pass.

The first pass does **not** include EtherCAT, F28388D support, physical multi-DSP
operation, a generic transport plugin system, torque/current control, or a CLA
LQR implementation.

## 2. Locked decisions

| Area | Decision |
|---|---|
| DSP motion units | Native encoder counts and counts/second; no gear or SI conversion |
| Public ROS/UI units | Configurable geared-output units; radians and radians/second by default |
| Direct motor command | `DUTY`, normalized to `[-1.0, +1.0]`; it is not called torque or effort |
| DSP inner rate | Encoder/home-input snapshot and all six CLA controllers at 5 kHz |
| SPI stream rate | 1 kHz baseline; the Pi master may later request 5 kHz for one DSP after timing validation |
| Encoder decoders | Axes 0–2 use eQEP at `×4`; axes 3–5 use both A/B edges on CPU2 eCAP1–6 at `×4`; four auxiliary sensors use CPU2 XINT1–4 at `×2` |
| Inner controllers | Independent position and velocity discrete transfer functions on CLA1; duty bypasses the controller |
| High-level control | C++ on the Pi real-time loop; LQR stays on the Pi initially |
| ROS integration | `ros2_control` hardware component and controller manager |
| UI data rate | Approximately 50 Hz, decoupled from the 1 kHz SPI stream and 5 kHz DSP loop |
| Configuration source | Versioned YAML on the Pi; host owns encoder/gear conversion and sends raw limits/setpoints to DSP |
| Physical DSP count | One now; configured endpoint registry and mock multi-DSP tests now |
| Recording | Explicit UI start/stop; binary full-SPI-rate data written outside the RT thread |
| Network scope | Same trusted Wi-Fi/LAN only; no cloud service or internet discovery |
| ROS distribution | Jazzy, matching Ubuntu 24.04 and the Project NUEVO baseline |
| DSP project form | Two independent CCS executables, CPU1 and CPU2; RAM-only during initial iteration |
| Motor board | Shared EN, six separate STBY, 10 kHz PWM; no exposed SF/current feedback |
| Homing | Six active-low GPIO switches with internal pull-ups, polled by CPU1 at 5 kHz |
| External E-stop | Purely physical; no DSP status or control wire |

## 3. Electronics and DSP pin assignment

The authoritative wiring, voltage-interface decisions, pin tables, peripheral
budget, and M0 hardware gates are in
[electronics.md](electronics.md).

One translated encoder channel now has a clean measured 3.3 V waveform. The
project remains blocked from energized multi-axis testing until all translated
channels, exact controlCARD pins, CPU2 aggregate encoder rate, homing inputs,
and the independent stop/enable circuit pass
the documented gates.

## 4. Derived interface budgets

Reference encoder scale and PWM limits are maintained with the hardware
interface in [electronics.md](electronics.md). The 5 kHz control and 1 kHz/10 MHz
transaction timing is maintained in
[spi_protocol.md](spi_protocol.md). Recording rates and ownership are
maintained in [ros2_interfaces.md](ros2_interfaces.md).

## 5. System architecture

```text
Browser
  │  LAN HTTP/WebSocket, ~50 Hz plots
  ▼
Python UI backend ───── ROS services/topics ─────┐
  │                                              │ CPUs 0–2, non-real-time
  ├── configuration persistence                  │
  ├── session/download management                │
  └── authentication                             │
                                                 ▼
                                   controller_manager / registry
                                                 │ validated RT handoff
CPU 3, isolated                                  ▼
  C++ ros2_control update loop: controller → read/write → fixed ring
                                                 │ SPI ioctl, 1 kHz baseline
                                                 ▼
                                      F28379D CPU1 + CLA1
                                                 │ 5 kHz snapshot/control
                              encoders → controllers → MC33926 outputs
```

ROS services, DDS serialization, logging, configuration file writes, WebSockets,
and disk I/O must never execute in the CPU 3 real-time loop.

## 6. DSP firmware design

### 6.1 Timing model

Target timing for the current board:

| Function | Rate | Trigger/owner |
|---|---:|---|
| MC33926 PWM | 10 kHz | ePWM hardware; SLEW is pulled low through 1 kΩ |
| Encoder snapshot and home-input poll | 5 kHz | ePWM-synchronized CPU1 control ISR |
| CLA control calculation | 5 kHz | CPU1-triggered CLA task |
| SPI command/telemetry | 1 kHz | Pi transaction + DSP DMA |
| Diagnostics counters | 1–10 Hz | CPU1 background |

The CPU1 control ISR will snapshot encoder values and the six home inputs at a fixed PWM phase,
write one coherent CPU-to-CLA message, and trigger the CLA. The CLA end-of-task
path will apply motor outputs every 200 µs. Every fifth result becomes the
baseline 1 kHz SPI telemetry snapshot; a later validated 5 kHz stream can publish
every result without changing the wire frame. The uncontrolled
“main loop runs as fast as possible” behavior in the current integration code is
not the final scheduling model.

### 6.2 Processor responsibilities

**CPU1**

- Boot, board initialization, and safety state machine.
- SPIA slave DMA and frame validation.
- eQEP1/2/3 snapshots.
- Six active-low homing inputs polled with internal pull-ups.
- Coherent eQEP and CPU2 encoder snapshot validation.
- Coherent CLA input/output handoff.
- Software E-stop/disarm handling, command watchdog, and telemetry assembly.

**CPU2**

- Own eCAP1–6 and XINT1–4 encoder interrupts.
- Decode both A and B edges for axes 3–5, producing signed `×4` counts; decode
  both A edges/sample B for four auxiliary sensors, producing signed `×2` counts.
- Publish aligned counts, rates, event/overrun counters, and a sequence number to
  CPU1 through fixed shared memory; perform no control or motor output.
- Enforce a configured aggregate event-rate ceiling established by M0 HIL tests.

**CLB1–4** remain unused in the baseline. They are the first on-chip fallback if
the CPU2 interrupt-rate gate fails, but no CLB firmware is built speculatively.

**CLA1**

- Run all six axis controllers from one coherent input snapshot.
- Support per-axis disabled, position transfer function, velocity transfer
  function, and duty bypass.
- Clamp every output to the configured duty limit.
- Reset controller state on arm, disarm, fault, and mode changes.
- Atomically replace complete controller coefficient/state blocks between tasks.

**CLA2** remains unused.

### 6.3 Axis modes

Each axis has exactly one active mode:

| Mode | Setpoint | CLA behavior |
|---|---|---|
| `DISABLED` | ignored | output disabled/zero according to board stop policy |
| `POSITION` | native int32 encoder count | position-error transfer function produces normalized duty |
| `VELOCITY` | native encoder counts/second | filtered velocity-error transfer function produces normalized duty |
| `DUTY` | `[-1, +1]` | bypass the controller and clamp to configured duty limit |

Velocity is derived from encoder-position differences at the CLA/control rate
and passed through one configurable first-order low-pass filter. A true torque
mode is deferred until `FB` is calibrated and a current controller is designed.

On entry to position mode, the internal reference starts at the current measured
position. Motion begins only after a fresh `CONTROL` command.

### 6.4 General discrete-time controllers

Store separate SISO transfer functions per axis for position and velocity. At
the fixed sample period `Ts = 200 µs`, each controller evaluates:

```text
u[k] = Σ(B[i] e[k-i], i=0..NB) - Σ(A[i] u[k-i], i=1..NA)
```

`A[0]` is normalized to `1.0`. `NA = NB = 16`, so each fixed array contains 17
IEEE-754 `float32` coefficients; the host zero-pads shorter configured arrays.
CLA1 uses a Direct Form II Transposed realization with bounded preallocated
state. Position error is in counts, velocity error is in counts/second, and
output is normalized duty.

The DSP validates finite coefficients, order, `A[0]`, sample rate, and revision;
stages complete A and B arrays while disarmed; then atomically swaps them at a
control boundary on configuration commit. State resets on arm, disarm, fault,
mode change, coefficient change, or explicit reset. The safety duty clamp is
outside the general controller. Generic anti-windup is not inferred from A/B
coefficients and must be designed explicitly if a controller requires it.

At 200 MHz, each 5 kHz tick provides 40,000 CLA cycles. Six simultaneously
active order-16 controllers require at most 198 coefficient multiply terms per
tick (`6 × (17 B + 16 A)`), leaving ample compute headroom for state updates and
clamps. Store only the active coefficient sets in CLA data RAM; configuration
staging remains CPU-owned because commits are disarmed. One 16-float working
state per axis is shared between position and velocity because mode changes
reset it. Active coefficients plus working state consume about 2,016 bytes
before message/metadata storage. M2 must still audit the linker map, move
unrelated `.bss`/`.data` out of CLA data RAM, and measure the real deadline with
a GPIO timing pulse and the deadline counter; the estimate is not acceptance
evidence.

### 6.5 Safety state machine

```text
BOOT → DISARMED → ARMED
          ▲          │
          │          ├── DISARM command
          │          └── fault / timeout → FAULT
          └──── CLEAR_FAULT (conditions healthy) ── FAULT
```

Rules:

- Boot always ends in `DISARMED`; outputs never retain a prior enabled state.
- `ARM` is accepted only with valid configuration, no latched software fault,
  healthy CPU2 encoder/CLA state, and recent valid SPI traffic.
- `DISARM` immediately forces the board-defined safe output state.
- A command timeout defaults to 50 ms, disables outputs, latches a communication
  fault, and requires `CLEAR_FAULT` followed by `ARM`.
- CRC/version/target-ID failures never refresh the watchdog.
- Stale or duplicate control sequences do not change outputs.
- Software E-stop, invalid encoder data, CLA failure, or CPU2 decoder overrun
  causes a latched fault.
- Clearing a fault never arms the system.
- Software limits supplement physical limits; they do not replace them.

## 7. SPI DMA protocol

The complete authoritative v4 wire layout, command payloads, telemetry fields,
DMA ownership, watchdog behavior, and validation gates are in
[spi_protocol.md](spi_protocol.md).

Implementation work must remove the current v2/v3 frame definitions and the
GPIO123/XINT1 frame boundary before the host and DSP codecs are declared
compatible.

## 8. Configuration model

The canonical file is `config/system.yaml`. It is versioned and contains the
configured DSP endpoints plus per-axis motor/encoder profiles, host conversion,
and limits. The example values describe the reference Maxon assembly, while the
schema permits other brushed motors and A/B encoders.

```yaml
schema_version: 1
config_revision: 1

control:
  dsp_control_rate_hz: 5000
  spi_stream_rate_hz: 1000
  command_timeout_ms: 50

dsps:
  - id: dsp0
    device_id: 1
    spi_device: /dev/spidev0.0
    spi_speed_hz: 10000000
    expected_protocol: 4
    axes:
      - axis: 0
        joint_name: dsp0_joint0
        motor_part_number: "148867"
        gearhead_part_number: "203116"
        encoder_part_number: "110514"
        motor_nominal_voltage_v: 24.0
        motor_polarity: 1
        encoder_cycles_per_motor_rev: 500
        quadrature_multiplier: 4
        index_present: true
        gear_ratio_numerator: 91
        gear_ratio_denominator: 6
        encoder_direction: 1
        zero_offset_counts: 0
        position_min_rad: null
        position_max_rad: null
        velocity_limit_rad_s: null
        duty_limit: 0.02
        current_feedback_present: false
        position_controller:
          denominator_a: [1.0]
          numerator_b: [0.0]
        velocity_controller:
          denominator_a: [1.0]
          numerator_b: [0.0]
        velocity_filter_hz: 20.0
    auxiliary_encoders:
      - sensor: 0
        name: dsp0_aux_encoder0
        enabled: false
        encoder_cycles_per_rev: 500
        quadrature_multiplier: 2
        direction: 1
        zero_offset_counts: 0
        max_event_rate_hz: null
        public_unit: radians
```

All six axes and four auxiliary encoders are explicit; implicit cloned defaults
are avoided for safety. An auxiliary decoder remains disabled until its scale,
direction, and maximum event rate are configured.
Unknown machine travel limits remain `null`, prevent arming, and must not
silently receive permissive defaults. The initial `0.02` duty limit corresponds
to approximately 0.48 V average at a 24 V bus; the current-limited bench supply
remains the primary protection during first motion.

Configuration flow:

1. The non-RT config service loads and validates YAML.
2. On startup, it probes the configured DSP endpoint while disarmed.
3. It computes output-unit conversion locally, stages motor polarity, raw limits,
   raw-unit controller coefficient arrays, and auxiliary decoder configuration,
   then commits the complete revision.
4. The DSP acknowledges and reports the same active configuration revision.
5. Only then may the hardware component become armable.
6. UI edits use the same validator and service.
7. Successful edits are written to a temporary file, `fsync`ed, and atomically
   renamed over the canonical YAML.
8. Failed DSP application leaves the previous active file and revision intact.

Hardware-specific choices such as MC33926 pin routing and slew mode belong in a
reviewed board profile, not in an unrestricted web control. Encoder cycles,
gear ratio, direction, zero, and public display units remain editable because
they affect host conversion, not DSP hardware timing.

## 9. Raspberry Pi, ROS 2, recording, and web UI

The authoritative node ownership, ros2_control interfaces, complete topic and
service inventory, multi-DSP behavior, real-time boundary, recording path, web
UI contract, and package layout are in
[ros2_interfaces.md](ros2_interfaces.md).

The execution constraints remain: one physical DSP with 5 kHz local control and
1 kHz SPI streaming in the first pass; multiple configured/mock DSPs tested
immediately; approximately 50 Hz UI
updates; recordings only on explicit request and under one minute; LAN-only
access.

## 10. Implementation milestones

Milestones are gated by evidence, not by code being present.

### M0 — Prove the pin, voltage, decoder, and stop architecture

Deliverables:

- Target top-level layout created and `DSP_28379D` reorganized under
  `dsp/f28379d/{cpu1,cpu2,shared}` without prefixed package/folder names.
- Maxon 148867/203116/110514 reference profile recorded in `system.yaml`.
- The pin allocation in `PLAN/electronics.md` checked against the controlCARD and actual MC33926
  wiring boards, including all six home switches and four auxiliary encoders. The
  already-wired eQEP1–3 dock pins remain unchanged.
- Point-to-point wiring and GPIO ownership table for the controlCARD/dock,
  H-bridge PCB, encoder receivers, Pi SPI, E-stop, and power supply.
- The tested MC3486/TXS0108E path replicated and scoped for all 23 connected
  encoder channels; no 5 V encoder output reaches a DSP GPIO.
- All seven CPU2 eCAP/XINT decoders proven simultaneously at the aggregate
  worst-case rate defined in `PLAN/electronics.md`, or a documented
  CLB/external-counter/reduced-rate replacement architecture.
- Current-limited, one-motor bench procedure beginning at 0.5 A and 2% duty.
- Logic-analyzer capture plan for PWM, chip select, and SPI mode.

Exit criteria:

- No GPIO conflict remains.
- No unresolved 5 V/3.3 V interface remains.
- All six motor encoders and four auxiliary sensors have a measured
  implementation path. The seven CPU2 edge-interrupt decoders have zero lost
  events and documented CPU2 headroom at the accepted aggregate rate.
- PWM/slew, shared-EN, and per-axis-STBY behavior match the MC33926 contract
  in `PLAN/electronics.md`.
- Machine travel limits and the physical E-stop behavior are documented.
- The bench can remove motor energy independently of software.

### M1 — Freeze protocol v4 and host codec

Deliverables:

- `PLAN/spi_protocol.md` frozen with exact offsets, enums, units, CRC, sequence
  rules, and result codes.
- Allocation-free C++ encoder/decoder.
- DSP parser/packer updated to the same frame.
- Golden frames shared by C++ and DSP tests.
- Mock `DspSession` capable of deterministic control and fault injection.

Exit criteria:

- Every message type passes golden-vector, corrupt-CRC, bad-version, non-finite,
  stale-sequence, and limit-validation checks.
- Pi and DSP agree on one SPI mode, verified with a logic analyzer.

### M2 — Make DSP control deterministic and fail-safe

Deliverables:

- PWM corrected for MC33926 slew mode.
- Shared EN and all six STBY outputs implement the locked safe state; the
  independent physical E-stop remains outside DSP control.
- Six homing inputs and the agreed one-axis-at-a-time homing sequence.
- Explicit safety state machine and watchdog.
- Coherent CPU1 → CLA → motor timing path at 5 kHz.
- Position, velocity, and duty modes with atomic coefficient changes.
- Fault/config/state telemetry.

Exit criteria:

- Boot, malformed traffic, and stale traffic cannot energize a motor output.
- Disarm, timeout, and software E-stop force the documented safe state.
- Mode changes cannot produce a stale-reference jump.
- CLA/control deadline counter remains zero in a 10-minute no-motor test.

### M3 — Validate all encoder and motor-axis paths

Deliverables:

- eQEP1/2/3 wiring and scaling verified.
- CPU2 eCAP1–6/XINT1–4 decoders implemented for axes 3–5 and four auxiliary
  sensors with event/overrun diagnostics and coherent CPU1 handoff.
- Encoder sign, index, zero offset, and velocity filtering verified.
- One MC33926 axis tested at 0.5 A/2% duty before either limit is increased or
  the wiring is replicated.

Exit criteria:

- Six output revolutions produce 182,000 counts on each of the six `×4` motor
  encoder paths, with the expected sign.
- CPU2 decoders report no missed/overrun events at the accepted simultaneous
  motor and auxiliary rates; otherwise the M0 replacement architecture is used.
- Each enabled auxiliary sensor reports the expected raw and public-unit motion
  without exposing a motor command interface.
- Host output-unit commands convert to the expected raw count/count-rate values,
  and each raw DSP mode works on one unloaded motor within configured limits.
- Direction reversal, limit violation, E-stop, and disconnect all
  produce the documented safe behavior.

### M4 — Build the Pi real-time ROS 2 path

Deliverables:

- Docker/laptop environment and host-setup diagnostic.
- `interfaces`, `bridge`, `robot`, `ui_bridge`, and `bringup` packages.
- ros2_control hardware component, standard bench controllers, and diagnostics.
- CPU affinity, scheduling, memory locking, and RT/non-RT handoff.

Exit criteria:

- Fresh laptop and Pi build instructions work without developer-local paths.
- Mock mode runs without DSP hardware.
- Physical mode sustains 5 kHz DSP control and 1 kHz SPI streaming for 10 minutes
  with zero CRC errors, zero stale command application, and no RT deadline
  misses.
- Jitter and transaction-duration distributions are saved with the test report.

### M5 — Prove multi-DSP behavior with mocks

Deliverables:

- Configured endpoint registry and probe service.
- Namespaced devices/joints and independent sessions.
- Automated zero/one/two-DSP, disconnect, duplicate-ID, and mismatch tests.

Exit criteria:

- All required scenarios in `PLAN/ros2_interfaces.md` pass.
- No test requires more than one physical DSP.
- Documentation clearly separates tested mock multi-DSP support from unmeasured
  physical multi-DSP operation.

### M6 — Add full-rate recording

Deliverables:

- Fixed binary format and format version.
- RT ring and non-RT writer.
- Start/stop/status/list/download services.
- Parser/inspection tool for test validation.

Exit criteria:

- A 60-second, one-DSP 1 kHz recording has the expected record count, ordered
  sequences, correct config snapshot, and zero dropped records.
- Forced disk and slow-writer failures do not disturb the control loop.
- Multi-mock recording preserves device identity.

### M7 — Add the LAN web UI

Deliverables:

- Authenticated DSP registry, control/configuration pages, 50 Hz plots,
  diagnostics, and recording workflow.
- Laptop mock mode for UI development and tests.

Exit criteria:

- The user can probe multiple mock DSPs, configure each independently, command
  all three modes, record, stop, and download a session.
- Invalid values are rejected consistently by UI and backend.
- Browser refresh/disconnect does not affect DSP safety behavior.
- No default account or ephemeral authentication secret remains.

### M8 — Integrated RAM release candidate

Deliverables:

- CPU1 and CPU2 RAM build instructions and verified paired images.
- Pi deployment, boot, recovery, wiring, calibration, and experiment procedures.
- One-command mock test suite and documented HIL test sequence.
- Known-limitations and test-results report.

Exit criteria:

- The complete Definition of Done in Section 12 passes from a clean checkout.

## 11. Test matrix

| Layer | Required checks |
|---|---|
| Protocol unit | Golden frames, endian/float packing, CRC, sequence wrap, all invalid inputs |
| DSP controller | A/B difference equation through order 16, saturation, state reset, mode switch, duty clamp, velocity filter |
| DSP safety | Boot state, arm rules, disarm, timeout, CRC storm, software/physical stop path, CPU2 decoder/CLA failure |
| SPI HIL | 1/5/10 MHz ramp, agreed mode, 1 kHz baseline, long run, unplug/reconnect; then optional one-DSP 5 kHz test at 10 MHz and validated 20 MHz if needed |
| Encoder HIL | A/B sign, index, ×4 motor/×2 auxiliary scale, zero, all seven CPU2 decoders at aggregate maximum rate, missed/overrun detection |
| Motor HIL | One axis first, all modes, reversal, bench-supply current limit, soft limit, physical E-stop |
| Host RT | cyclictest baseline, scheduling/affinity, deadline/jitter histogram, no RT allocation/I/O |
| ros2_control | Interface claiming, mode switching, lifecycle, inactive/offline device behavior |
| Multi-DSP mock | 0/1/2 devices, isolation, reconnect, duplicate ID, version mismatch |
| Recorder | 60 seconds, record count/order, disk error, slow writer, multi-device identity |
| UI | Login, validation, arm confirmation, plots, reconnect, recording/download, accessibility basics |

## 12. Definition of Done

The first pass is complete only when:

1. The DSP always boots disarmed and no invalid/stale frame can enable an output.
2. MC33926 PWM, shared EN, per-axis STBY, and stop behavior match the
   reviewed PCB wiring.
3. All six motor encoders and four auxiliary encoder-only sensors have correct
   direction and scale at their required simultaneous speeds.
4. Position-count, velocity-count-rate, and duty modes work independently with
   bounded outputs, while ROS/UI conversions remain entirely on the Pi.
5. Motor/encoder/gear conversion, auxiliary encoder configuration, and controller
   coefficients can be loaded from YAML and edited in the UI; the Pi persists
   them safely, converts commands and telemetry, and sends only acknowledged raw
   decoder configuration, limits, and A/B arrays to the DSP.
6. One physical DSP sustains 5 kHz snapshot/control and 1 kHz streaming for
   10 minutes with the M4 communication and deadline criteria.
7. ROS/UI tests support at least two simultaneous mock DSPs without cross-talk.
8. A 60-second recording completes with no dropped records and downloads through
   the UI.
9. Communication loss, software E-stop, physical E-stop, software limit, and process restart
   all produce their documented safe behavior.
10. A clean machine can build the ROS workspace and both CPU1/CPU2 RAM
    configurations using only documented dependencies and commands.

## 13. Explicitly deferred work

Add these only after the first-pass Definition of Done:

- Index channels or motor outputs for the four auxiliary encoder-only sensors.
- Any ADC acquisition in the current PCB profile; all 14 planned channels remain reserved.
- DSP SPI-master control of TMC5160 or other peripherals.
- Physical multi-DSP operation.
- EtherCAT transport and migration to F28388D.
- True motor-current/torque mode.
- Current-feedback and auxiliary ADC acquisition on the reserved channels.
- Flash boot configurations and production flashing.
- CLA LQR or dynamically uploaded controller matrices.
- Browser-side raw-data caching or CSV as the primary recorder format.
- Internet exposure, cloud access, or deployment outside a trusted LAN.

## 14. Known current-code gaps

Existing DSP files are a useful starting point, not proof that the integrated
system is complete:

- [`motor.c`](../dsp/f28379d/cpu1/motor.c) now has the locked 10 kHz pin map, but
  protocol-driven arm/STBY sequencing is not implemented.
- [`main.c`](../dsp/f28379d/cpu1/main.c) handles timeout by setting position references to
  zero; that can command motion toward encoder zero instead of disabling outputs.
- The CLA currently implements one legacy position-in-counts PID at 1 kHz; the
  target 5 kHz position/velocity A/B controllers and duty mode are not implemented.
- The SPI `cmd` field is parsed but ignored.
- The current telemetry lacks measured velocity, DSP state, command
  acknowledgment, config revision, and device identity.
- Axes 3–5 currently depend on the safe-idle CPU2 project; the first-pass design adds
  gated eCAP/XINT decoders for them and the four auxiliary sensors as specified
  in `PLAN/electronics.md`.
- The current SPI test and DSP comments disagree about mode numbering; the final
  mode must be measured and documented.
- The controlCARD GPIO allocation is closed for prototype wiring in
  `PLAN/electronics.md`; continuity from the custom MC33926 PCB remains to be
  checked.

These gaps are addressed by M0–M4 rather than patched piecemeal.
