# Build, deployment and operation

Use the same Git repository on the laptop and Pi. The systemd units run from `/home/toby/Project-Crazy-Diamond` on the Pi. Save and commit any tuning changes you want to share before pulling. Recordings, uploaded programs, credentials and build products are machine-local.

## Build on the Pi

```bash
cd ~/Project-Crazy-Diamond
git pull --ff-only
bash scripts/build_ros.sh
npm --prefix web_ui ci
npm --prefix web_ui test
npm --prefix web_ui run build
```

Use Node.js 22.12 or newer in the Node 22 series for the web build. ROS 2 Jazzy is installed under `/opt/ros/jazzy` on the Pi's PREEMPT_RT Linux setup.
`build_ros.sh` discovers generic packages in `ros2_ws/src` and optional packages
in `robots`. It builds and runs tests with colcon. There is no DSP rebuild in a
host-only change. See the [DSP hardware guide](../../dsp/f28379d/README.md#build-and-upload) for RAM/FLASH builds and upload.

For a host deployment, finish active experiments, use Stop All and stop the affected services before replacing their build. Restart those services after the tests pass.
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

To install or update the supplied units on this Pi, run the following from the repository root after a successful build:

```bash
sudo install -m 0644 scripts/diamond-*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now diamond-bridge diamond-data diamond-ui diamond-phantom
```

The Python service scripts use the `config/fastdds-loopback.xml`
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

The rack-and-pinion scale is `500 × 4 × (91/6) / (30π) = 321.846662696944` counts/mm. See the [bench table](../../doc/README.md#heart-phantom-bench) for the configured slider ranges. Numeric input, ROS and CSV references can exceed a slider range. Travel bounds are display settings.

Disable a motor, move it manually, then Reset zero to establish another origin.
The other axis can remain enabled. Browser reconnect/axis re-enable preserves
that origin. Bridge/DSP restart establishes a new origin and remains disarmed.

Reset is a `/bridge/command` operation applied by the realtime worker, not a
browser display offset. It captures `zero_counts[axis]` from the latest DSP
encoder count and clears that axis's target, reference, and controller history.
The bridge, Phantom and CSV playback share that origin and the axis direction described below. The UI waits for the bridge's
application acknowledgement before reporting success. Reset is rejected while
the axis is enabled or an active/holding runner owns it. Once reset, a stationary
axis should need no return-to-zero travel before a sine run. The robot's settling
wait still runs. `tests/phantom_startup.py` checks this at nonzero raw encoder counts.

### Axis direction and position units

Set `axisN.reverse_direction: true` in the project bridge YAML to reverse an axis. Keep its existing `motor_sign`, which matches the driver wiring to the encoder. The bridge applies the direction to both measured displacement and outgoing motor duty. The hardware readbacks in `/dsp/state` keep their original signs.

Choose `position_unit: mm` for a linear axis or `position_unit: deg` for a rotary axis. Calibration is positive and is measured at the mechanism output. For a rotary motor encoder, divide decoded counts per **output** revolution by 360. For example, a 500-cycle motor encoder with ×4 decoding and a 91/6 reduction gives 84.25925926 counts per output degree. Rotary position is continuous through ±180° and 360°. The slider bounds only define its displayed range.

```yaml
# Example entries under bridge.ros__parameters for a rotary axis
axis2.position_control: true
axis2.position_unit: deg
axis2.counts_per_deg: 84.25925925925926
axis2.minimum_deg: -180.0
axis2.maximum_deg: 180.0
axis2.reverse_direction: true
axis2.controller_profile: research_pid
```

The existing linear entries use `counts_per_mm`, `minimum_mm`, and `maximum_mm`. Linear units and forward direction are the defaults. Heart Phantom explicitly reverses all three installed linear axes. Restart the bridge with the rig idle to apply calibration and capture fresh zeros. Read back the active units and direction under **Resources** or from `/bridge/state`.

With `d = −1` for a reversed axis and `+1` otherwise, the bridge computes `position = d × (raw_counts − zero_counts) / counts_per_unit` and `dsp_reference_counts = zero_counts + d × reference × counts_per_unit`. Manual PWM, CSV duty and Step Recorder injection also change output polarity when reversed. Controller trace duties and raw DSP duty readings share wire polarity.

Open the PCD UI at `http://<pi-address>:8000/`. The dashboard and Resources list use the project manifest’s axes in DSP channel order (one to six). Axis controls are independent of the
optional robot panel. Expand an axis to use its slider, numeric target, zero or plot.
The axis gear opens that axis's settings. Changing pages clears the selection. Drag the divider to resize the settings panel. The browser remembers its width.

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

Set the same environment in four terminals on the development host, then run one script per terminal from the repository root. The distinct ports and ROS domain keep the simulated bench separate from the physical services.

```bash
export ROS_DOMAIN_ID=67 DIAMOND_PORT=18782 PHANTOM_PORT=18783
export PHANTOM_PANEL_URL=http://localhost:18783/
# Terminal 1
bash scripts/run_bridge.sh true
# Terminal 2
bash scripts/run_data.sh
# Terminal 3
bash scripts/run_ui.sh
# Terminal 4
bash scripts/run_phantom.sh
```

Open `http://localhost:18782/` and verify the DSP badge shows MOCK. Use a separate checkout for mock experiments when preserving the production library, profiles or recordings matters. The mock plant checks commands, state and data flow. Realtime scheduling and SPI timing require the physical bridge.
