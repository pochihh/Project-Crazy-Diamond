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
travel bounds and SPI device. For another project, supply `projects/<id>/project.json`, `bridge.yaml` and
`controllers/`, then set `DIAMOND_PROJECT=<id>` for build and both bridge/UI
services. Profile parameters persist when saved; startup profile selection
remains in the project YAML.

Both commissioned axes use a 500-cycle encoder,
×4 decoding, 91/6 gearbox, 30-tooth module-1 pinion: 321.846662696944 counts/mm.
Current travel bounds are ±50 mm relative to the captured startup origin.

Disable a motor, move it manually, then Reset zero to establish another origin.
The other axis can remain enabled. Browser reconnect/axis re-enable preserves
that origin. Bridge/DSP restart establishes a new origin and remains disarmed.

Open the PCD UI at `http://<pi-address>:8000/`. Axis controls are independent of the
optional robot panel. The Logger tab selects signals and records/downloads CSV;
the Runner tab selects a saved program or uploads one. Any ROS node can operate
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
