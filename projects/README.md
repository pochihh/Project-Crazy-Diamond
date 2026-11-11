# Project configuration and controller profiles

PCD owns the DSP transport, real-time host, compiled axis controllers, ROS
interfaces, recorder and web UI. Projects supply calibration, startup selections,
labels and reusable tuning profiles. External robot nodes/CAD/panels remain the
next integration step; see [the robot contract](../PLAN/ROBOT_AND_CONTROLLERS.md).

| File | Purpose |
|---|---|
| `heart_phantom/project.json` | Project title, axis names and model placeholders |
| `heart_phantom/bridge.yaml` | Calibration, travel, polarity, duty caps, reference speed, SPI device and startup profile IDs |
| `heart_phantom/controllers/*.yaml` | Reusable controller type and parameters; no axis hardware settings |

Both axes start with `bench_pid`. Each receives an independent parameter copy
and controller state. In the web UI, open **Controller settings** on an axis:

- **Apply parameters** tunes the running controller in one update.
- **Load / Reload** explicitly applies the selected saved profile to that axis.
- **Save profile** updates its YAML file; other axes keep their active settings.
- **Save as new profile** creates a YAML file and selects it for this axis.

Saving persists the profile. UI profile selection is for the current host
session; set `axis0.controller_profile` / `axis1.controller_profile` in
`bridge.yaml` for startup selection. Hardware limits and calibration stay there.
Duty-limit edits still require disable and last until restart.

```yaml
schema_version: 1
id: bench_pid
type: pid
sample_period_s: 0.0005
parameters:
  kp: 0.0005
  ki: 0.001
  kd: 0.0
```

The filename must match `id` (`bench_pid.yaml`). IDs use 1–63 lowercase letters,
digits, underscores or hyphens. Unknown types/parameters, incomplete parameter
sets, non-finite values, schema mismatches and a different loop period are rejected.
Files are replaced atomically; Save as never overwrites an existing profile.

## Compiled controllers

The registry and implementation dispatch are in
[`controllers.hpp`](../ros2_ws/src/bridge/include/bridge/controllers.hpp);
[`pid.hpp`](../ros2_ws/src/bridge/include/bridge/pid.hpp) implements PID.
PID is currently available. LQR awaits the owner's state model.

To add a controller, implement its bounded `reset()` / `step()` behavior,
parameter application and state-continuity rules, then add its descriptor,
variant and dispatch in `AxisController`. Rebuild the ROS host. The descriptor
supplies parameter names, units, defaults and bounds to the generic UI. The
current command payload allows at most 16 parameters; expand and remeasure only
if a concrete controller needs more. Additional measured/estimated state inputs
for LQR must be designed with the supplied model, not inferred from PID's error.

The loop runs at **2 kHz**. Controller code must not allocate, lock, access files,
call ROS or perform network work. YAML parsing and validation occur in the
management thread; fixed-size updates are applied at a cycle boundary. Type
changes require disable and reset state. Same-type PID tuning preserves the
integral contribution and rebases it for P/D changes to reduce output jumps.
Every CSV sample includes active type, profile ID and complete parameters.

The host retains travel checks, reference slew, tracking checks, finite-output
validation, duty caps and stop/fault handling. Position uses a **20 mm/s reference
slew limit**, not an acceleration-limited trapezoidal trajectory. Direct PWM
bypasses the position controller and that ramp.

## Another project

Copy `heart_phantom/`, change `project.json`'s ID and YAML `project_id`, and set
`controller_directory` to the new project's profile directory. Select the same
project at build and runtime:

```sh
export DIAMOND_PROJECT=my_robot
bash scripts/build_ros.sh
bash scripts/run_bridge.sh true
# In another shell, export the same DIAMOND_PROJECT, then:
bash scripts/run_ui.sh
```

For systemd, set `DIAMOND_PROJECT` in both services, rebuild and restart them.
Restart starts disarmed and captures new startup zeros. The default project is
`heart_phantom`; its two configured axes map to DSP channels 0/1.
