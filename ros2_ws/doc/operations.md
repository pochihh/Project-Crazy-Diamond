# Build, deployment and operation

Canonical checkouts:

- Laptop: `/Users/toby/Projects/Project-Crazy-Diamond`
- Pi: `/home/toby/Project-Crazy-Diamond`
- Shared Git repository: `pochihh/Project-Crazy-Diamond`; deployed releases use `main`.

Use Git to synchronize source. Do not run from the old `crazy-diamond-stage2`
checkout. Generated builds, `.local` credentials/evidence, recordings and uploaded
programs are ignored. Runtime programs and recordings need their own backup.

## Build on the Pi

```bash
cd ~/Project-Crazy-Diamond
git pull --ff-only
bash scripts/build_ros.sh
npm --prefix web_ui test
npm --prefix web_ui run build
```

ROS 2 Jazzy is installed under `/opt/ros/jazzy` on the Pi's PREEMPT_RT Linux setup.
`build_ros.sh` discovers generic packages in `ros2_ws/src` and optional packages
in `robots`. It builds and runs tests with colcon. There is no DSP rebuild in a
host-only change. Follow the linked DSP guide for CCS RAM/FLASH builds and upload.

Stop the affected services before replacing an active build, then restart them.
A bridge restart starts disarmed and captures new startup zeros.

`projects/<project>/bridge.yaml` sets `disarm_on_deadline_miss`. It is a startup-only
ROS parameter: edit the project YAML and restart the bridge to change it. The
current `heart_phantom` project sets it to `false`; omitted projects default to
`true`. Every missed 500 μs deadline is counted and logged in either mode. With
`true`, a miss also disarms an enabled system (including I/O-only operation).
With `false`, the worker continues without issuing catch-up bursts; delayed ticks
can stretch CSV timing. SPI errors, DSP faults/reset recovery, invalid controller
output and failure to establish realtime scheduling still stop or prevent drive.
Mock runs always log timing without automatic deadline disarm.

## Services

| Service | Process / script | Purpose |
|---|---|---|
| `diamond-bridge` | `scripts/run_bridge.sh false` | Physical SPI, RT controllers/runner, recorder, ROS API |
| `diamond-data` | `scripts/run_data.sh` | Persistent CSV library and Python import |
| `diamond-ui` | `scripts/run_ui.sh` | PCD UI on port 8000 |
| `diamond-phantom` | `scripts/run_phantom.sh` | Optional robot node/panel on port 8001 |

Unit templates are in `scripts/`. All use the canonical working directory and
ordinary CPU affinity 0–2; only the bridge worker moves itself to isolated CPU 3,
FIFO 80. The bridge requires RT priority and locked-memory limits from its unit.
A robot/data/UI restart does not automatically restart the bridge. No service
automatically starts motion or a recording.

The Python service scripts use the verified `config/fastdds-loopback.xml`
transport profile. The installed ROS deployment is local to the Pi. Browser
access uses the LAN. Remote ROS clients require an explicit discovery/transport
configuration change; embedding a panel does not require that change.

```bash
sudo systemctl status diamond-bridge diamond-data diamond-ui diamond-phantom
journalctl -u diamond-bridge -n 50
journalctl -u diamond-data -u diamond-phantom -n 50
```

Set `PHANTOM_PANEL_URL=http://<pi-address>:8001/` in the phantom service environment
if the default `<hostname>.local` URL is not resolvable by browser clients.

## Configuration and manual operation

`projects/heart_phantom/bridge.yaml` selects profiles, calibration, polarity, caps,
UI slider ranges and SPI device. For another project, supply `projects/<id>/project.json`, `bridge.yaml` and
`controllers/`, then set `DIAMOND_PROJECT=<id>` for build and both bridge/UI
services. Profile parameters persist when saved; startup profile selection
remains in the project YAML.

Axes 0, 1 and 2 use the same 500-cycle encoder,
×4 decoding, 91/6 gearbox, 30-tooth module-1 pinion: 321.846662696944 counts/mm.
Axis 2 uses CPU1 eQEP3. Its PWM, STBY and DIR are dock pins 50, 52 and 62; encoder A, B and optional index are 127, 128 and 130. Shared motor enable is pin 71. The project selects `bench_pid`, motor sign −1 and duty cap 1 for axis 2. The Phantom sine program continues to use axes 0 and 1.

The position sliders display ±50 mm relative to the captured startup origin.
This is not an enforced travel limit; numeric input, ROS and CSV references can
exceed it. Neither enabling outside that range nor PWM motion across it is blocked.

Disable a motor, move it manually, then Reset zero to establish another origin.
The other axis can remain enabled. Browser reconnect/axis re-enable preserves
that origin. Bridge/DSP restart establishes a new origin and remains disarmed.

Reset is a `/bridge/command` operation applied by the realtime worker, not a
browser display offset. It captures `zero_counts[axis]` from the latest DSP
encoder count and clears that axis's target, reference, and controller history.
All consumers, including Phantom and CSV playback, use the same coordinates:
`position_mm = (dsp.position - zero_counts) / counts_per_mm`. The raw DSP encoder
count intentionally remains unchanged; the reference sent to the DSP is
`zero_counts + reference_mm * counts_per_mm`. The UI waits for the bridge's
application acknowledgement before reporting success. Reset is rejected while
the axis is enabled or an active/holding runner owns it. Once reset, a stationary
axis should need no return-to-zero travel before a sine run. The robot's settling
wait still runs. `tests/phantom_startup.py` checks this at nonzero raw encoder counts.

Open the PCD UI at `http://<pi-address>:8000/`. The dashboard and Resources list use the project manifest’s axes in DSP channel order (one to six). Axis controls are independent of the
optional robot panel. Expand an axis to use its slider, numeric target, zero or plot.
On each collapsed card, position is on the left, target and error are in the middle, and the gear, Enable switch and duty meter are on the right. Expanding adds Reset zero and the target entry on one row, followed by the ruler slider and position plot. Click the card surface or its disclosure button to expand or collapse. The commands, switch and slider operate independently. The settings column starts empty. An axis gear selects that axis for Position/PWM mode and tuning. The Settings button opens web preferences. Clicking the selected gear or Settings again clears the column. Clicking the workspace preserves the selection. Switching pages clears the panel and its highlight. Drag the divider or focus it and press the left/right arrow keys to adjust the column width. The browser saves the width locally. On narrow screens the column docks below the workspace. The light meter band
tracks recent magnitude peaks and falls at 25 percentage points per second.

The DSP card shows INIT until feedback is synchronized, IDLE for disarmed, RUNNING
for active, and FAULT for a DSP fault. Start uses the bridge system-enable command.
Previously selected axes can become enabled on Start. The DSP Stop button sends the existing DSP disarm command.
The expanded table distinguishes invalid commands received by the DSP from Pi
telemetry/link errors. Pi fault clearing has its own action.

On the Data page the runner loads or uploads CSVs, and the logger below selects
signals, records and downloads CSVs. Any ROS node can operate
those same backend services. Start on the phantom panel enables and records.
Closing a browser does not stop a run. Stop All disables all motors and general
I/O; runner-specific Stop affects its listed channels and associated recorder.

## Faults and counters

Inspect raw DSP state/counters separately from host faults. Clear DSP fault only
while disarmed, then clear the host fault and explicitly enable/start again.
Fault clear does not clear DSP lifetime diagnostic counters; DSP power cycling
does. Host counters persist until the bridge restarts.

`recordings/realtime-events.jsonl` and the bridge journal retain overrun and missed
period counts. Repeated identical unavailable/invalid SPI feedback is summarized
at most once per second, while cumulative telemetry-error counts continue to
increase. A powered-off DSP therefore produces host errors; verify that they stop
increasing after reconnection. Queue overflow and recording-write failures are
reported separately. Global maximum cycle/lateness values are service-lifetime
statistics, not maxima for just the latest experiment.

## Mock development

Launch `scripts/run_bridge.sh true`, data, UI and phantom scripts in a separate
`ROS_DOMAIN_ID`, with test HTTP ports if physical services are also present.
Mock scheduling is ordinary Linux scheduling; its deadline counts are not a
physical realtime acceptance result. The mock plant validates workflows, not
real controller dynamics. Never run two physical bridge owners on the SPI device.
