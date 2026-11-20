# Controllers and profiles

`AxisController` is the compiled realtime controller block. It owns the selected
feedback implementation, the reference limiter state and tuning configuration.
The bridge supplies target, measured position, counts/mm and the 0.0005 s period;
the block exposes its applied reference and returns normalized motor duty.

PID and Step Recorder are implemented. LQR awaits the owner's state model. Adding a new controller
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
  d_filter_hz: 0.0
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

- `bench_pid`: the existing gains, 50 Hz D filtering and explicit 20 mm/s slew limiting.
- `research_pid`: the same gains with passthrough reference and D filtering bypassed.
- Gain units remain duty/count, duty/(count·s), and duty·s/count. There is no
  automatic gain-unit conversion when displaying millimeters.
- PID differentiates error with a backward difference at 2 kHz:
  `e = (reference_mm - position_mm) * counts_per_mm`,
  `raw_d = (e - previous_e) / 0.0005`, `D = kd * filtered_d`,
  `duty = motor_sign * clamp(kp * e + I + D, -duty_limit, duty_limit)`.
  D is zero on the first tick after reset. `d_filter_hz = 0` bypasses filtering;
  otherwise a first-order low-pass uses `tau = 1 / (2*pi*d_filter_hz)` and
  `filtered_d += dt/(tau + dt) * (raw_d - filtered_d)` (backward Euler).
  Cutoff is tunable from 0 to 1000 Hz. Filter history is private to the axis,
  retained across live cutoff updates, and cleared by controller reset.
- Only D is filtered: P, I and reference processing are unchanged. D still
  differentiates error, so target changes contribute a filtered derivative kick;
  this does not switch to derivative-on-measurement. Lower cutoff smooths D more
  and adds lag. Large P errors can still demand full duty. Without filtering,
  one encoder count in one tick contributes `2000 * kd` duty (20% for Kd 0.0001).
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
Older schema-2 PID YAML files that omit `d_filter_hz` load with zero/bypass, retaining
their previous behavior; the next Save writes the field explicitly. Live API
updates must supply all four PID parameters. The compiled descriptor drives the
UI cutoff input and its range; Apply uses the same verified read-back as gains.

Every controller/profile/gain/duty-limit command returns its actual configuration
in `/bridge/command_result`, captured immediately after application by the RT
worker, together with the command ID, axis and controller revision. It is not an
echo of the request or a later state snapshot. The HTTP API checks requested
parameters against this read-back before reporting success. The settings panel
shows the active gains/revision separately from unsent edits and confirms verified
Apply results. Subsequent commands from other ROS nodes can still change the
active configuration; `/bridge/state` and the panel reflect those changes.

Calibration, polarity, the UI slider range and duty cap are separate axis settings.
`minimum_mm`/`maximum_mm` set the slider display range (currently ±50 mm), not
travel protection. Numeric targets, ROS references and CSV programs may exceed
that range. Enabling outside it is allowed and PWM is not stopped on crossing it.
Reference values must still be finite and representable as signed 32-bit DSP
encoder-count references. The bridge has no tracking-error threshold: a large
reference error remains an input to the controller and does not disable outputs.
Future application-specific motion protection, such as detecting a stalled axis,
belongs in a separate safety module; that module is not implemented yet.
Logging can include input target, applied reference, PID gains and `d_filter_hz`,
limiter mode/rate, requested duty and DSP-reported duty. Initial axis configuration
is also included in CSV comments.

Source: `bridge/include/bridge/controllers.hpp`, `pid.hpp`, `profiles.hpp` and
`control.hpp`. The UI edits the compiled descriptor and profile fields; it does
not implement the controller.

## Signal diagrams and frame identity

The axis inspector renders the Mermaid block from the selected controller's Markdown file. These files also list the signal suffixes for the Logger. [PID](diagrams/pid.md) · [Step Recorder](diagrams/step_recorder.md)

The v5 DSP already publishes three identifiers. The session and acknowledged command sequence together identify a specific Pi→DSP command. The session and telemetry sequence together identify a 5 kHz DSP snapshot. The host observes snapshots at approximately 2 kHz. The SPI reply is prepared before the current transfer, so matching by row number or a fixed one-tick offset is incorrect.

The RT bridge keeps a fixed ring of 64 outgoing commands indexed by sequence, checks the complete session and sequence on lookup, and attaches the matched values to the incoming snapshot before handing it to the recorder. This applies to PID, Step Recorder, direct PWM and digital/analog outputs. Each CSV row keeps its one DSP timestamp. The following signals can also be selected by ROS clients through the existing Logger and preview interfaces.

| Signal | Meaning |
|---|---|
| `dsp_session`, `dsp_command_sequence`, `dsp_sequence` | Session, acknowledged command, and snapshot identifiers from the DSP |
| `host_tick`, `ack_host_tick`, `ack_matched` | Receiving Pi iteration, originating Pi iteration, and whether an exact command match was available. Pi tick numbers restart with the bridge. |
| `ctrl_input_session`, `ctrl_input_sequence` | Session and earlier DSP snapshot available when the Pi prepared the acknowledged command |
| `axisN_ctrl_type`, `axisN_ctrl_revision` | Controller implementation and configuration revision that produced the acknowledged trace |
| `ack_operation`, `ack_arm_mask`, `axisN_ack_sent_duty`, `ack_do_mask`, `ack_ao0_raw`, `ack_ao1_raw` | Actual outgoing SPI command fields. Use the DSP output channels for applied outputs. |
| `axisN_ctrl_valid` | A matched control-cycle command for this enabled position controller, with healthy DSP feedback. Direct PWM, ARM and DISARM have no active feedback-controller trace. |
| `axisN_ctrl_r_mm`, `axisN_ctrl_y_mm`, `axisN_ctrl_error_counts` | Reference, feedback used in computation, and their difference in encoder counts |
| `axisN_ctrl_p_duty`, `axisN_ctrl_i_duty`, `axisN_ctrl_d_duty`, `axisN_ctrl_kp` | Signed contributions and Kp used on that tick. PID I is captured before the integration update. |
| `axisN_ctrl_u_duty`, `axisN_ctrl_u_im_duty`, `axisN_ctrl_u_g_duty` | Feedback output, additive excitation, and capped float duty sent to the plant |
| `axisN_requested_duty`, `axisN_output_duty`, `axisN_position_mm` | The DSP's requested and applied duty and the position observed in this snapshot |

Use `ack_matched` and `axisN_ctrl_valid` when interpreting controller columns. A repeated acknowledgement can legitimately appear with a newer encoder sample. A skipped command can lack any observed DSP snapshot. Missing history is marked invalid and never filled with the most recent controller values. DSP applied duty is a firmware readback; motor-terminal voltage is a separate measurement.

## Step Recorder

1. Disable the selected axis. Use its existing zero, or move it and use **Reset zero**. The reference throughout this experiment is exactly 0 mm at that origin.
2. Open the axis gear, select the **step_recorder** saved profile and **Load / Reload**. Adjust **Kp**, signed **step_duty** (0.05 means 5% in DSP polarity), **baseline_s**, **step_s** and **recovery_s**. Apply and check the active revision.
3. Press **Start + record**. The bridge opens a CSV, enables only the requested experiment axis (other already enabled axes retain their selection), and starts the timeline once the DSP reports the axis armed. At 2 kHz each duration is rounded to the nearest tick. Each run captures its own timing and amplitude. Same-type Kp adjustments take effect live.
4. The proportional output is `u = motor_sign × Kp × (0 − position_mm) × counts_per_mm`. Injection is zero during baseline and recovery. During the step it equals the signed amplitude. The outgoing duty is `u_g = clamp(u + u_im, −axis_limit, +axis_limit)`. Kp zero gives an open-loop motor step. The duty cap can clip excitation.
5. At completion or **Stop + save**, the runner disables the experiment axis and closes the recording. Stop All and bridge faults interrupt the run. Download the CSV under **Data → Recordings**. The step replaces the previously loaded runner program; loading a saved CSV selects that CSV again.

The step CSV automatically includes the aligned frame identifiers, injected/feedback/total duty, DSP duty readbacks, position, raw counts, digital input/output masks, six analog inputs, two analog outputs, tick and validity. `step_tick` is zero-based; use it to locate actual phase edges or identify duplicated/skipped commands. The recorder follows the acknowledged command through the SPI pipeline at both ends of the run. An existing recording or runner must finish before starting another experiment.

From ROS use `/bridge/command` with operation `step_start`, zero-based `axis` and its current `controller_revision`. Use `step_stop` with the same axis for early completion. The normal command result acknowledges acceptance by the worker. Runner and Logger state are shared with other clients.

## Adding a compiled controller

Add an independent implementation and parameter descriptor to `controllers.hpp`. Keep the controller's per-axis state inside its implementation and populate the numerical trace during its tick. For additional internal signals, add fixed-size fields to `ControllerSignals` and catalog entries in `recorder.hpp`. Add a matching Markdown file in `doc/diagrams/<type>.md`, a project YAML profile, then rebuild. Frame matching is shared by every implementation. A future ILC with an inner PID can own its update law and memory without a runtime cascade editor.
