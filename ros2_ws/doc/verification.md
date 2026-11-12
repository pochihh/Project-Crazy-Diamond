# Verification and limits

Experiment-tools acceptance completed on 2026-11-12 on the bench Pi with the
existing v5 DSP firmware. All six ROS packages build; 32 colcon tests and six UI
tests pass, and the production TypeScript/Vite build passes.

## Measured results

| Check | Result |
|---|---|
| ROS-only mock experiments | Hold/stop/loop, shared ownership, file library, phantom sine/CSV and selected recording passed without the web server |
| Physical ROS experiments | All completion modes and both phantom sources passed; finite stop produced exactly 500 consecutive rows starting at row zero |
| Existing physical controls | Position, direct duty, live PID tuning/profile save/reload and DIO/AIO command/telemetry regressions passed |
| 60 s loaded manual control | 120,284 consecutive full-catalog recording rows; eight state streams; zero new errors, missed periods or drops |
| 60 s loaded phantom playback | 120,463 consecutive full-catalog rows; four state and four preview streams; zero new errors, missed periods or drops |
| Browser | Robot panel rendered; robot-originated run/recording visible in generic tabs; saved-file load/start/stop, selected recording and CSV download exercised |
| Optional robot | Removing its mock node hid the dashboard panel while the core controls remained available |
| Independent bridge restart | Data/UI/robot services stayed running; physical ROS experiments passed again with zero host errors, missed periods or drops; maximum cycle 320.26 µs |

Both load tests ran CPU burners on cores 0–2 while the physical worker stayed on
isolated CPU 3 at FIFO 80. The runner test checked every row transition, including
loop boundaries, and every DO0 command: ten high ticks per 500-tick cycle, with no
skipped rows. This proves command scheduling; physical pin pulse width has not
been measured with an oscilloscope in this pass. DIO/AIO regression checked DSP
telemetry, not new external electrical loopbacks.

One early live attempt **did interrupt**: SPI transactions exceeded the 500 µs
period and one invalid telemetry frame occurred. The runner stopped, outputs
disabled and its CSV closed. Inspection showed 112 accumulated missed periods
and one host telemetry error. A short CPU-governor comparison did not reproduce
the issue or establish its cause; the original `ondemand` governor was restored.
The subsequent bounded regressions and both loaded runs passed with zero counter
increments. This intermittent fault is **not claimed fixed**. Service-lifetime
maxima include that incident and configuration work, so they are not run-specific
timing maxima. Preserve the journal and `recordings/realtime-events.jsonl` when
investigating a recurrence; every reported overrun/missed period remains logged.

Detailed machine-local evidence lives in `.local/experiment-results/` on the Pi:
`physical-ros.json`, `physical-http.json`, `physical-controllers.json`,
`physical-io.json`, `physical-after-restart.json`, `loaded-manual/`, `loaded-runner/`, and the mock equivalents.
Recordings remain in `recordings/`; they are intentionally outside source control.

## Automated checks

- C++ core: calibration, enables, faults, PID continuity, timestamp wrapping,
  controller passthrough versus explicit slew limiting and lock-free handoffs.
- C++ data: atomic two-axis/DO rows, ten-tick pulses, hold/stop/loop, ownership,
  fault interruption, selected recording columns and filename collisions.
- Profile tests: schema, units/period, round-trip persistence and shared-copy isolation.
- Python CSV tests: exact names, ignored-column warnings, invalid rows/numbers,
  conflicting output sources and file-name checks.
- Phantom waveform tests: phase, period quantization and pulse width.
- API/UI tests: relays, input validation, recording downloads, controller command
  payloads and plot rendering behavior.

`tests/experiments_ros.py` exercises the complete experiment path using ROS only,
including the phantom coordinator. It tests repeated library selection, all
completion modes, first-row recording alignment, waveform DO0 command width,
controller passthrough and rejection of competing writers. It ends disabled.
Pass `--physical` only intentionally on the bench. Existing position/PWM,
controller-profile, I/O and realtime-soak tests remain regression checks.

## Established baseline

Before this pass, the two-axis v5 DSP/ROS system completed a 60-second loaded run
with 120,163 consecutive samples and no new communication errors, missed deadlines
or queue drops. Three intentional DSP power cycles then recovered without Pi
service restarts; the original CSV closed cleanly, host fault stayed latched,
and control resumed after explicit clear. See the historical bridge-cleanup report.

Firmware acceptance previously covered CPU1/CPU2 RAM/FLASH and standalone boot,
dock DIO/AIO loopbacks and functional CPU2 quadrature stimulus. This does not imply
full-speed validation of all six physical motors/encoders. Those limitations,
active-drive power-loss testing, hardware watchdog/reset-cause changes and the
owner-supplied LQR model remain separate work.
