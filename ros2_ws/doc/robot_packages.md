# Robot packages and Heart Phantom

PCD owns the hardware transport, generic controllers, logger, runner, direct-axis
UI and Stop All. Robot-specific nodes, experiment logic and panels live under
`robots/` as separate ROS packages. They can be moved into a separate repository
without editing PCD's dashboard. The build discovers both `ros2_ws/src` and
`robots`; removing robot packages leaves generic PCD available.

## Embedded-panel contract

Publish `/robot/panel` (`interfaces/msg/RobotPanel`) periodically:

- `version: 1`
- `id`: stable robot identifier
- `name`: human-readable label
- `panel_url`: HTTP(S) URL serving the package-owned panel

PCD embeds that panel at the top of the dashboard in a sandboxed iframe with
scripts and same-origin access for the panel's own API. It hides the entire slot
when no supported manifest has arrived for two seconds. Stop All remains outside
the iframe. There is no runtime component loader, schema-driven panel editor or
robot kinematics code in the PCD bundle. First-pass panel sizing is responsive;
no cross-frame command protocol is required.

The panel's HTTP endpoints route to its robot ROS API. Generic Logger/Runner tabs
observe the bridge's actual state, including operations started elsewhere. If
the robot node/panel disappears, playback already loaded into PCD continues under
its selected completion mode; use the generic runner or Stop All to stop it.

## Phantom experiment

`phantom_robot` serves its panel on port 8001 and advertises every 0.5 s. Set
`PHANTOM_PANEL_URL` to the browser-reachable URL when hostname resolution differs;
`PHANTOM_PORT` changes the server port. The panel uses its own `/state`, `/files`
and `/command` endpoints. ROS clients call `/phantom/command` directly.

`phantom_interfaces/srv/Command` fields:

| Field | Meaning |
|---|---|
| `operation` | `start` or `stop` |
| `source` | `sine` or `csv` |
| `period_s` | Shared sine period; 0.001…300 s, rounded to a 0.5 ms tick |
| `amplitude_mm[2]`, `offset_mm[2]` | Independent axis amplitudes/centers |
| `phase_deg` | Axis 1 phase relative to axis 0; default 0° |
| `pulse_width_ms` | DO0 high at each period start; default 5 ms = 10 ticks; zero disables |
| `approach_speed_mm_s` | Sine startup move to zero, default 5 mm/s; positive and finite |
| `completion` | `hold`, `stop`, `loop`; panel defaults to loop |
| `filename` | Saved file from the shared `data/programs/` library |
| `csv`, `overwrite` | Optional upload text and explicit replacement flag |
| `signals[]` | Logger selection; empty selects phantom's reference/position/duty/gain/DO0/runner defaults |

Sine equations are `offset0 + amplitude0*sin(2πt/T)` and
`offset1 + amplitude1*sin(2πt/T + phase)`. One period is prepared outside RT and
executed by the generic runner. Both first samples must be zero: nonzero starts
are rejected before changing outputs. With zero offsets, phases 0° and 180°
are valid; a 90° phase is not. An offset/phase combination is allowed only if
its first sample is also zero. Roundoff within 1e-9 mm is normalized to zero.
Pulse width is rounded to ticks and must be
shorter than the period. The panel reports actual quantized timing in its state.
DSP actuation still has SPI/firmware timing granularity; host tick timing alone
is not an oscilloscope measurement of a physical pin.

CSV mode defaults to a saved-file picker, with upload in an optional expandable
section. It requires exactly the three recognized channels
`axis0_reference_mm,axis1_reference_mm,do0`. Extra unknown columns warn/ignore;
other recognized outputs are rejected for this phantom mode.

Start requires fresh, connected, fault-free bridge feedback with no active
run/recording. Sine Start first disables axes 0/1, takes their measured positions,
and loads a separate `phantom-approach-zero` program into the existing RT runner.
Its first row is the measured position; synchronized linear ramps reach zero
without either axis exceeding `approach_speed_mm_s`. DO0 stays low and no
experiment recording runs during preparation. The robot then holds zero until
both measured positions remain within ±0.1 mm for 0.3 seconds. Failure to settle
within the planned ramp duration plus ten seconds cancels Start and requests
runner Stop. Approach programs are limited to 300 seconds.

After settling, the robot stops the approach, loads the sine and starts row zero,
DO0 and recording together. The zero coordinate is the bridge's existing startup
or manually reset zero; this procedure does not reset encoders or define a new
zero. PID gains and profile reference shaping stay in force. A sine still starts
with its specified velocity; this removes the initial position step, not the
power demand of a fast waveform or the unresolved SPI timing issue.

The panel reports preparation/settling phases while the Start request is pending
and leaves Stop available. ROS clients can identify the approach through the
bridge's program name; `/phantom/command` returns success once the experiment has
started. Allow up to 360 seconds for a sine Start response. A Stop from the robot,
generic runner or Stop All cancels preparation and prevents a delayed Start.
Stale feedback, bridge restart or a bridge fault also cancels preparation.

CSV Start continues to load its specified rows and start recording immediately;
the sine zero-start rule does not rewrite uploaded experiments.
Stop cancels playback, disables the owned motors, clears DO0
and saves the associated CSV. Repeated Start does not replace an active run.
An explicit Stop during preparation cancels the pending Start.

The axes' selected controller profiles remain in force. Choose `research_pid`
(passthrough) when studying the unshaped sine/CSV reference. The robot does not
silently change PID gains or bypass output/fault limits. Waveform settings are
applied when starting a new run; changing an active waveform requires Stop/Start.

The panel displays each axis's active limiter and the sine's peak speed,
`2π × amplitude / period`. A ±20 mm, 1 s sine requires approximately 125.7 mm/s;
a 100 mm/s slew limit still reshapes it. Select **Passthrough / no shaping** in
each disabled axis's Controller Settings when the reference must stay unshaped.
This does not guarantee physical tracking: PID dynamics, duty limits and the
mechanism still determine measured motion. Tiny floating-point residues below
1e-12 display as zero in the panel and logger preview; command/CSV values retain
their original precision.
