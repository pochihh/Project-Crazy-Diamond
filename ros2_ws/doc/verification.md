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

## Sustained physical-motion recordings — 2026-11-12

The small ±0.3 mm / 0.25 s runner waveform above validates command timing, but
its encoder recordings show negligible mechanical motion. A new attempt using
that waveform was stopped after 23.75 s: axis 0 moved only 0.0093 mm and axis 1
remained stationary, despite nonzero DSP-reported duty. The 47,511-sample CSV is
preserved as `recordings/2026-11-12_01-35-54.csv`; it is not a completed soak.
No controller or firmware fault was established from this observation.

For actual motion, the sustained runner checks use the previously verified
±5 mm envelope at an 8 s period, axes 180° apart, with the existing bench PID
profiles and a temporary 25% duty cap. DO0 is commanded high for ten ticks (5 ms)
at each period start. No CPU burners or extra UI streams were used in these runs.

| Run | Recording | Result |
|---|---|---|
| 2 minutes | `2026-11-12_01-38-37.csv` | PASS: 240,315 consecutive samples; both axes approximately ±4.99 mm |
| 5 minutes | `2026-11-12_01-41-22.csv` | PASS: 600,253 consecutive samples; both axes approximately ±4.99 mm |

Both runs recorded the full signal catalog, verified row-zero alignment, all
runner-row transitions including loop boundaries, and ten-tick DO0 commands.
Neither added host telemetry errors, missed periods, dropped samples, or DSP
invalid/rejected/timeout counts. Outputs and recording were disabled afterward;
the original duty caps and control modes were restored. As in the existing soak,
each test captures fresh axis zeros at its start. The earlier intermittent SPI
incident remains unresolved and its evidence is unchanged.

`tests/realtime_soak.py --verify-motion` selects this slow waveform for runner
mode and rejects a run unless both encoders demonstrate bidirectional travel
past ±2.5 mm. It checks live feedback after 16 s and the saved full-rate CSV.
The original small command-timing waveform remains the default without that flag.

```bash
python3 tests/realtime_soak.py --physical --runner --verify-motion \
  --seconds 120 --output .local/physical-recording-20261112/motion-120s
python3 tests/realtime_soak.py --physical --runner --verify-motion \
  --seconds 300 --output .local/physical-recording-20261112/motion-300s
```

Run evidence and further per-cycle/tracking analysis are in
`.local/physical-recording-20261112/` on the Pi. The command-timing checks still do
not constitute an oscilloscope measurement of electrical pulse width.

## Saved-CSV physical playback — 2026-11-12

The follow-up tests explicitly used Phantom `source=csv` and the unchanged saved
`data/programs/phantom_example.csv`, not sine-generation mode. Its 4,000 rows
command ±1 mm on axes 0/1 in phase, a 2 s period, and DO0 high for the first ten
ticks (5 ms). The input SHA-256 is
`bdd4d8fa74fde62e1442bc51cef0811433db2406d82a569fd36d03fd28d1bd01`.
The same bench PID profiles, 20 mm/s limiter and temporary 25% duty cap remained
in force; the input file, controller gains and DSP firmware were not changed.

| Run | Recording | Playback/recording result |
|---|---|---|
| 2 minutes | `2026-11-12_10-46-48.csv` | PASS: 240,415 consecutive samples |
| 5 minutes | `2026-11-12_10-49-14.csv` | PASS: 600,389 consecutive samples |

Every recorded target and DO0 command matched the corresponding input CSV row;
row zero, every transition and every loop boundary were checked. Neither run
added telemetry errors, missed deadlines, sample drops, or DSP diagnostic errors.
There were already 128 accumulated missed periods before these tests, with the
recent event history showing disarmed outputs. That baseline was not cleared;
the existing timing issue remains unresolved and its evidence is preserved.

Measured travel was approximately −0.80 to +0.82 mm for the ±1 mm commands.
This is verified physical movement, but it is not perfect trajectory tracking:
the current controller/plant falls short of the requested amplitude. No tracking
accuracy requirement was supplied, and no PID retuning was performed. Full-rate
recordings are retained for analysis. Electrical DO0 pulse width was not measured.

The saved-CSV option validates the file's channels, finite values and travel
range before starting, then compares every saved command to the source row.
`--verify-motion` uses thresholds at 25% and 75% of each input channel's range
(−0.5/+0.5 mm for this file). Post-run analysis additionally checks every complete
profile cycle for measured travel; the aggregate live guard alone does not prove
uninterrupted movement throughout a run.

```bash
python3 tests/realtime_soak.py --physical --runner --csv-profile phantom_example.csv \
  --verify-motion --seconds 120 --output .local/csv-playback-20261112/run-120s
python3 tests/realtime_soak.py --physical --runner --csv-profile phantom_example.csv \
  --verify-motion --seconds 300 --output .local/csv-playback-20261112/run-300s
```

Evidence, per-cycle/tracking analysis and the export archive are in
`.local/csv-playback-20261112/` on the Pi. Both tests ended with outputs and
recording disabled and the previous duty caps restored. As with the existing
soak, each run captured fresh axis zeros while disabled.

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
