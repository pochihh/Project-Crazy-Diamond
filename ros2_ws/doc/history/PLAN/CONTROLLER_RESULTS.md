> Historical snapshot. Current system documentation: [index](../../README.md).

# ROS 2 controller-profile acceptance

Updated: 2026-11-11. Implemented and deployed to the bench Pi. DSP firmware and
protocol remain unchanged from the accepted v5 FLASH images.

## Delivered

- Compiled PID registry with parameter names, units, defaults and bounds exposed
  through ROS and HTTP. LQR remains unavailable pending the owner's state model.
- Per-axis controller panel in the existing settings position; complete live
  parameter updates, explicit profile load/reload, Save and Save as.
- Project YAML profiles with schema/type/period validation and atomic writes.
  Shared profiles give each axis independent active parameters and PID state.
- PID tuning preserves integral output and rebases P/D contributions at a control
  boundary. File I/O and parsing run outside the real-time thread.
- CSV records each axis's active type, profile ID and complete parameter snapshot.
- The panel explains the existing 20 mm/s maximum reference advance: 0.01 mm per
  2 kHz cycle, with no acceleration/deceleration profiling.

See [usage, YAML schema and code extension](../PROJECTS_GUIDE.md) and
[ROS/HTTP interfaces](ros2_interfaces.md#controller-profiles-2026-11-11).

## Verification

- All three ROS packages build; **11 tests pass**. Checks include live PID
  continuity, atomic parameter application, independent axis settings, type-change
  interlock, invalid parameters, profile schema/path handling, atomic replacement
  and rejection of Save as over an existing file.
- TypeScript/Vite production build passes; **six UI tests pass**.
- Browser inspection on the isolated mock UI verified panel layout, editing,
  Apply, unsaved-versus-saved status, explicit reload and reference-rate display.
- Mock integration: **5,201 samples**, live updates/shared-file save/explicit
  reload and complete CSV parameter snapshots pass. Zero telemetry errors/drops;
  six mock deadlines missed under ordinary scheduling, not RT acceptance.
- Physical live-tuning/profile integration: **5,197 samples**, both axes remained
  enabled during updates and same-type reload. Saving one axis's shared profile
  did not modify the other axis until explicit reload. Zero new telemetry errors,
  deadline misses or dropped samples.
- Full physical regression: both axes ±50 mm and return, hold, disabled-only zero,
  invalid-target rejection, ±3% PWM commands, and recording for over 60 seconds.
  **131,321 consecutive samples**, zero telemetry errors, deadline misses or drops.
  Maximum host cycle **307.686 µs** of a 500 µs period; maximum lateness **17.659 µs**.
  Recording: `session-1794426465682265124.csv`.

Evidence: `.local/controller-results/{build.log,mock-result.json,physical-result.json,regression-result.json,state-before.json,state-after.json}`.
Both services were left running, with motors/DO/DAC commands disabled, the
original `bench_pid` settings restored, and test profiles removed.

## Baseline observation and limits

Before deployment, the old physical bridge had a latched host telemetry fault:
one accumulated telemetry error and seven deadline misses after a longer run.
The link was again connected with advancing frames, all six encoder paths valid
and DSP fault zero. This was captured in `state-before.json` before replacing
code. Its cause was not established; this pass does not claim to fix that
pre-existing intermittent problem. It did not recur in the measured new runs.
The subsequent [stability investigation and diagnostic logging](REALTIME_STABILITY.md)
also did not reproduce it; that record includes loaded tests and deliberate
overrun tests validating persistent missed-period logs.

Only PID is implemented. Robot packages, LQR and acceleration-limited trajectory
profiles remain separate work. Saved profile contents persist, but selecting a
profile in the UI changes only the current session; startup selection stays in
`bridge.yaml`. Duty limits remain axis configuration, require disable to change,
and are not part of reusable tuning profiles. Detached-shaft tests do not
validate loaded mechanisms or every future gain choice.

Rollback installation: `/home/toby/.local/diamond-backups/before-controllers-20261111.tar.gz`
on the Pi. Stop both services before restoring matching ROS source/build/install,
project configuration and web assets from that archive.
