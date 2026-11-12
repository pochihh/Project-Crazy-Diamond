# Controllers and profiles

`AxisController` is the compiled realtime controller block. It owns the selected
feedback implementation, the reference limiter state and tuning configuration.
The bridge supplies target, measured position, counts/mm and the 0.0005 s period;
the block exposes its applied reference and returns normalized motor duty.

PID is implemented. LQR awaits the owner's state model. Adding a new controller
requires C++ code, a compiled parameter descriptor and rebuild. There is no
runtime code loading or block-diagram editor.

```yaml
schema_version: 2
id: research_pid
type: pid
sample_period_s: 0.0005
parameters:
  kp: 0.001
  ki: 0.001
  kd: 0.0
reference:
  type: passthrough
  max_rate_mm_s: 20.0
```

`passthrough` applies the requested reference directly; `max_rate_mm_s` is inactive.
`slew_limit` advances the block's internal reference by at most `max_rate_mm_s * dt`
per tick. It is a velocity/slew limit, not an acceleration-limited trajectory.
The positive finite rate is stored even when bypassed so switching modes preserves
its setting. Unknown reference modes/fields and old schema versions are rejected;
all shipped profiles have been explicitly migrated to schema 2.

- `bench_pid`: the existing gains plus explicit 20 mm/s slew limiting.
- `research_pid`: the same gains with passthrough reference behavior.
- Gain units remain duty/count, duty/(count·s), and duty·s/count. There is no
  automatic gain-unit conversion when displaying millimeters.
- PID differentiates error with a backward difference at 2 kHz:
  `e = (reference_mm - position_mm) * counts_per_mm`,
  `D = kd * (e - previous_e) / 0.0005`,
  `duty = motor_sign * clamp(kp * e + I + D, -duty_limit, duty_limit)`.
  D is zero on the first tick after reset. There is currently no derivative
  filter: reference steps produce a derivative kick, and one encoder count in
  one tick contributes `2000 * kd` duty (20% for `kd = 0.0001`).
- Integral contribution is stored in duty units. After calculating the output,
  `ki * e * dt` is accumulated unless it would drive an already saturated output
  farther into saturation; integration that unwinds saturation remains allowed.
  Saturation itself never adds an integral contribution. With `ki = 0`, I is
  cleared and remains zero, so P/PD has no hidden output bias.
- Live same-type gain/rate updates apply at a tick boundary. When the new Ki is
  positive, PID rebases I to reduce a P/D gain-change jump. With Ki zero, gain
  changes take effect directly; switching Ki to zero removes accumulated I.
- Changing controller type or switching passthrough/slew mode requires the axis
  disabled. Enable/reset initializes reference history from measured position.
- Direct duty skips the entire controller block, including reference limiting.

Profiles live in `projects/<project>/controllers/`. Each axis has private tuning
and history. Save writes YAML atomically; other axes keep their copies until an
explicit reload. Save as creates a new file. UI profile selection is session-only;
startup assignments live in `bridge.yaml`.

Every controller/profile/gain/duty-limit command returns its actual configuration
in `/bridge/command_result`, captured immediately after application by the RT
worker, together with the command ID, axis and controller revision. It is not an
echo of the request or a later state snapshot. The HTTP API checks requested
parameters against this read-back before reporting success. The settings panel
shows the active gains/revision separately from unsent edits and confirms verified
Apply results. Subsequent commands from other ROS nodes can still change the
active configuration; `/bridge/state` and the panel reflect those changes.

Calibration, polarity, ±50 mm bench travel bounds and duty cap are separate
axis/host settings. The bridge has no tracking-error threshold: a large reference
error remains an input to the controller and does not itself disable outputs.
Future application-specific motion protection, such as detecting a stalled axis,
belongs in a separate safety module; that module is not implemented yet.
Logging can include input target, applied reference, PID gains,
limiter mode/rate, requested duty and DSP-reported duty. Initial axis configuration
is also included in CSV comments.

Source: `bridge/include/bridge/controllers.hpp`, `pid.hpp`, `profiles.hpp` and
`control.hpp`. The UI edits the compiled descriptor and profile fields; it does
not implement the controller.
