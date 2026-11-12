# Verification and limits

Current experiment-tools acceptance is in progress. Replace this paragraph with
measured build/integration/hardware results before declaring the pass complete.

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
