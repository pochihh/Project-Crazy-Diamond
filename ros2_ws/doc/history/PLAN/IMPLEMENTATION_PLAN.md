> Historical snapshot. Current system documentation: [index](../../README.md).

# Implementation Roadmap

Updated: 2026-11-11

This is the agreed delivery order, replacing the earlier M0–M8 plan.
[Current system](CURRENT_SYSTEM.md) records what works;
[system design](ROS2_plan.md) owns requirements and open choices.

## 1. Clean documentation

Deliver a source-backed baseline, accepted future requirements, an ordered
roadmap, linked operating/hardware references, and explicit remaining questions.

Complete when the docs agree on rates, ownership, available telemetry, CSV
policy, recording scope, and the stage-2 boundary. Check links and source facts.
This stage does not change firmware or run motors.
Status: complete; the subsequent stage-2 work and results are documented separately.

## 2. ROS 2 and web UI on the current DSP firmware

**Outcome: drag position controls in a browser to control the two working axes.**

Status: complete for the unloaded two-axis bench on 2026-11-10.
See [acceptance evidence and limitations](STAGE2_RESULTS.md).

### Boundary

- Keep the working DSP firmware and command-v2/telemetry-v3 frames.
- Retain the checked-in 1 kHz Pi/SPI baseline and Pi-side PI/PID behavior.
  The 2 kHz target belongs to stage 3.
- One physical DSP; axes 0/1 for position control. CPU2, homing, new I/O, and
  arbitrary DSP controllers are not prerequisites.
- Implement the C++ ROS control path, Python backend, TypeScript UI,
  configuration, launch/build instructions, and a minimal mock mode.
- Add full-SPI-rate recording of available values, with disk work outside
  the control loop and UI start/stop/download.
- CSV checks, when playback is connected, run only in Python and display
  warnings. No extensive validator or duplicated CSV checks.
- Calibrated degrees/mm from the first UI; convert targets on the Pi while
  preserving the existing controller tuning.
- The two working axes are linear rack-and-pinion actuators: use mm, nominal
  scale 321.8466626969 counts/mm, and −50 to +50 mm controls. Capture startup zero
  from valid telemetry while disarmed, once per host-controller run; retain it
  across enable/disable and browser reconnects. Keep the full conversion formula.
- Add per-axis Reset zero: with that motor disabled, its current measured
  position becomes 0 mm and its held target becomes 0 mm. No physical homing yet.
- The Pi OS/RT configuration is verified. Include ROS 2 Jazzy installation
  because no existing ROS install was found. No web login layer is required
  for this trusted-LAN stage.
- Hold the final target on slider release and keep control/recording running
  through browser disconnect. Explicit stop and host/SPI faults still stop motion.
- Downloadable CSV is the recording format.
- Functional controls first; final visual design is stage 4.

### Sequence

1. Use the inspected Ubuntu 24.04.4/PREEMPT_RT host and preserved Pi source
   snapshot. Set up ROS 2 Jazzy/build tools and reconcile source differences
   without changing the working DSP. Locate CSV code if available; do not block
   manual control on locating it.
2. Integrate the existing codec/controller behavior with a bounded handoff
   from non-real-time commands to the real-time loop.
3. Connect backend/UI: two position sliders, target/actual position and duty,
   system/axis enable, connection status, stop, and recording.
4. Start/reconnect the host/DSP path disarmed and initialize targets without an
   enable jump. Browser reconnect only reloads active state; it does not disarm,
   reset targets, or re-arm hardware.
5. Verify with mocks, then test end to end on the existing two axes.

### Completion criteria

- Browser dragging in calibrated degrees/mm controls axes independently and
  together through Pi feedback and unchanged DSP firmware.
- Target/actual position and duty update live; unavailable capabilities are
  identified rather than shown as valid zero readings.
- System disable overrides axis requests; host/DSP reconnect does not re-arm.
- Slider release holds the target. Browser disconnect leaves control and
  recording running; browser reconnect restores the current display.
- Existing host error handling and DSP timeout behavior are preserved.
- A bounded bench recording captures every received SPI sample, downloads,
  and reports any lost data/writer error.
- UI/recording do not cause control deadline misses during the acceptance run;
  measured results are saved.
- Build, launch, mock use, and operation are documented.

Physical multi-DSP operation, robot-specific kinematics, complete auto workflows,
new I/O, and final styling are not stage-2 acceptance gates.

Implementation: one C++ ROS node with a dedicated real-time thread, Python/FastAPI,
and TypeScript/Vite. The working codec is shared with the original bench tool.
See [operation](STAGE2_OPERATION.md) and [acceptance results](STAGE2_RESULTS.md).

### Requested UI/function pass before protocol migration

The 2026-11-10 feedback in [UI_design.md](../../../../PLAN/UI_ref/UI_design.md) brings forward the
fixed-header/sidebar layout, Heart Phantom project identity, progress meters,
position/PWM selection, live Pi usage/ROS topic inspection, browser settings, and
clear project-versus-PCD source organization. Advanced controllers start with
project C++ code/configuration; no coefficient/block editor is required. This uses
the same DSP firmware and 1 kHz loop. Final styling/model details and the remaining
intermittent browser delivery lag still need follow-up.

## 3. Migrate DSP, SPI frames, and interfaces

**Outcome: preserve working UI control while adding the expanded DSP feature set.**

Active instruction, 2026-11-10: build and load RAM firmware first, test it through
the existing two-axis UI, then build/program FLASH and repeat the final regression.
Preserve the current source/build pair before loading. UI/host changes are limited
to what the firmware contract needs. The external robot package and PID/LQR/YAML
profile changes are [documented for discussion](ROBOT_AND_CONTROLLERS.md) before
implementation; do not combine that refactor with firmware acceptance.

Status: complete for the agreed bench scope on 2026-11-11. RAM/FLASH two-axis
regressions, standalone boot, dock DIO/AIO and CPU2 functional jumper counting
passed. Full-speed external CPU2 encoder testing is deferred; six-axis physical
motor validation is not implied. See [stage-3 results](STAGE3_RESULTS.md).

Accepted scope: Pi controllers; DSP duty/encoder/I/O endpoint; all six motor
encoders; 4 DI, 4 DO, 6 AI, 2 AO. Keep disabled-axis Reset zero; automatic
homing and auxiliary encoders are deferred. The concrete contract is protocol v5.

Deliver:

- A versioned protocol, host/DSP codecs, status/configuration behavior, and
  [the new telemetry requirements](spi_protocol.md).
- Latest values in one synchronized frame with one frame timestamp.
  No per-channel timestamp or cross-channel time-alignment subsystem.
- Target 2 kHz Pi/SPI operation, measured with the actual frame/workload.
- Working general DIO/AIO wired through the dock for testing, exposed to the
  UI and recorder. Hardware redesign follows later.
- Keep manual Reset zero. Future Pi homing and local DSP limit protection are
  separate work; general DI does not yet act as a limit interlock.
- Preserve Pi-side feedback control and explicit direct duty; no DSP A/B or FOC.
- Add the three CPU2 motor encoder paths and report measured polling timing.
  Record physical stimulus/rate coverage separately from software verification.
- Host/ROS/UI changes preserving stage-2 operation and extending recording.

Completion criteria:

- Shared codec checks cover encoding, CRC, malformed traffic, and added
  sequence/configuration behavior.
- Two-axis browser control and recording still pass.
- Assigned digital/analog channels work from the dock and match test stimuli
  or commanded outputs in UI and recordings.
- Manual zero, watchdog and explicit fault recovery match documented behavior.
- Report which encoder paths/rates have physical stimulus evidence; do not infer
  six-axis validation from the working two-axis regression.
- The integrated system meets the agreed 2 kHz target with saved timing and
  no unexplained sample loss. Resolve any measured shortfall before declaring
  this target complete.
- Firmware, host, wiring, configuration, and operating docs agree.
  Retain a known working firmware/host pair for regression/recovery.

### Following firmware acceptance: controller profiles

The 2026-11-11 ROS 2 pass replaces the project controller alias with a compiled
PID registry, reusable project YAML profiles and per-axis controller panels.
Complete parameter updates apply live at a control boundary. Type changes still
require disable; shared saved profiles require explicit reload on other axes.
LQR awaits the owner's state model; external robot-package integration remains
the next separate task. See [operation/development](../PROJECTS_GUIDE.md) and
[test results](CONTROLLER_RESULTS.md).

## 4. Fine-tune the web UI

Refine visual details after the functional system is usable. The first UI already
uses the requested grid background, bold borders, restrained color coding, and
engineering tables from `UI_ref/`; those references are not an exact specification.

Refine the system/control diagram, resource table, ROS/Linux status, plots,
labels, interaction feedback, accessibility, and layout. Add robot visualization
when its model and intended interaction are specified.

Complete when agreed styling/workflows pass without regressing control,
stop behavior, or recording.
