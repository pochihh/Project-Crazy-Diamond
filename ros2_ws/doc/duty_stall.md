# Full-duty motor stall investigation — 2026-11-12

## Reproduction and confirmed evidence

Rapidly dragging axis 0's target slider can leave the motor unresponsive while
the duty meter reaches 100%. The owner reports that the unloaded shaft can be
turned manually and disabling/re-enabling the axis restores operation.

The reproduction ran after the PID saturation fix and removal of host travel
trips, with the project's deadline disarm disabled and SPI/DSP gating retained.
Axis 0 used Kp = 0.0005, Ki = 0, Kd = 0.00001 and passthrough reference processing.

The 2 kHz recording `recordings/2026-11-12_13-22-23.csv` contains 109,954 rows over
54.9756 seconds. It includes 27.9326 seconds of unchanged encoder counts at full
commanded duty. During the captured stuck state:

| Signal | Observed value |
|---|---|
| Final target / measured position | +17.0 / −29.492305 mm |
| Raw encoder position | 31 counts, unchanged |
| DSP requested / reported output duty | −1.0 / −1.0 |
| Host system / axis 0 enable | Both true |
| DSP state / arm mask | ACTIVE / 0x01 |
| Host fault / DSP fault | None / 0 |
| Missed deadlines / host SPI errors | 0 / 0 |
| Input rejections / dropped samples | 0 / 0 |
| DSP frame and command sequences | Continued advancing |
| Axis 0 PWM, dock 49 | Owner measured 3.3 V while stuck |
| Axis 0 STBY, dock 51 | Owner measured 3.3 V while stuck |
| Shared EN, dock 71 | Owner measured 3.3 V while stuck |
| Driver supply | Owner measured 24 V while stuck, with little current consumption |
| Directly across the two motor leads | Owner measured 0 V while stuck |

DSP invalid-frame and command-timeout lifetime counters remained at their
pre-existing values (5 and 1); no new event occurred during this recording.
The trace shows rapid reversals immediately before motion stopped. A bridge
tracking/travel/communication gate did not disarm this reproduction.

## What remains unconfirmed

The board is not energizing the motor despite the observed DSP PWM/enable signals
and a 24 V supply. The current custom board does not expose MC33926 SF or current
feedback, so `dsp.fault = 0` does not rule out a driver power-stage fault.

The [MC33926 datasheet](https://www.nxp.com/docs/en/data-sheet/MC33926.pdf), section
11.3, describes latched short-circuit and overtemperature shutdown that can be
reset through its enable/disable inputs. This is a plausible explanation, not a
confirmed diagnosis. Board input logic, supply behavior and output wiring also
remain possible; a DC voltage reading cannot exclude an earlier transient.
Disable/re-enable additionally resets the controller target
to the measured position, so recovery by that action alone is not conclusive.
The owner deferred further hardware investigation after these measurements.

No DSP firmware or hardware protection was modified for this investigation.
The recording is downloadable from the web Data Logger. Additional Pi evidence
is in `.local/host-gate-cleanup/`: `stuck-snapshots.json`, `repro-analysis.json`,
build/test logs and before/after configuration snapshots. These test artifacts
are intentionally outside Git; the source and project policy are synchronized.
