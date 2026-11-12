> Historical snapshot. Current system documentation: [index](../../README.md).

> The current ROS topics, services, nested DSP telemetry and independent fault
> controls are described in [ros2_ws/README.md](../ROS2_WS_GUIDE.md). Continuous
> commands now use topics; the older service examples here apply only to discrete
> operations. DSP firmware remains v5.

# Stage-2 operation

Updated: 2026-11-11. This controls axes 0/1 using protocol-v5 DSP firmware. The filename is retained for existing links.
See [interfaces](ros2_interfaces.md) and [test results](STAGE2_RESULTS.md).

## Use the deployed system

Open **http://192.168.1.21:8000/** on the local network. If using the configured
wired subnet instead, use `http://192.168.2.10:8000/`. There is no login layer.

1. Confirm **CONNECTED** and **DSP 00 / PHYSICAL**. A mock run says SIMULATED HARDWARE.
2. Enable system, then Enable motor for each desired axis. The target initializes
   to measured position when a motor becomes active, so old disabled targets are
   not replayed. Enabling does not change the zero offset.
3. Drag the slider or enter a target and press Set/Enter. Targets are in mm,
   bounded to ±50. The reference slews at 20 mm/s by default; release holds target.
4. **Reset zero:** disable that motor, move manually, press Reset zero. Current
   measured position and held target become 0 mm. The other motor may stay enabled.
5. **Stop all** disables system and both motor requests. Recording continues until
   Stop & save. Individual motor disable affects only that motor's host permission.

Closing the page, losing Wi-Fi, or restarting the web service does **not** disable
motors or stop recording. Reopening reads active state. Restarting the C++ bridge
does disable motors and establishes new startup zeros. It never automatically
restores enable requests. If manually moved outside ±50 mm, reset before enabling.

The UI paints on display frames and plots against the single DSP frame timestamp,
with a 100 ms display buffer for ordinary network jitter. It does not smooth or
invent measured positions. A stalled WebSocket triggers an independent HTTP state
snapshot; old queued updates are discarded. **DATA DELAYED** means telemetry has
not arrived promptly, not that the real-time loop stopped. Complete network loss
still prevents fresh feedback/commands; the controller keeps its held target.
Commands time out after four seconds without automatically retrying a possibly
applied action. Check live state before retrying.

System/axis controls are host requests; Resources shows the DSP-confirmed arm
mask, fault, command acknowledgment and all six raw encoder counts. It also
provides four digital outputs and two DAC code controls, plus four digital and
six ADC inputs. Enable System with no motors selected for I/O-only testing.
Stop/disable/fault clears DO and DAC commands. Analog values are raw 0–4095;
see the dock wiring table before loopback testing. Home/limit handling is still
deferred. One operator is assumed; multiple tabs share controls.

## Calibration and tuning

Both axes use the confirmed nominal conversion:

```text
counts/mm = (500 cycles × 4 × 91/6) / (π × module 1 mm × 30 teeth)
          = 321.846662696944
position_mm = (raw_count - zero_count) / counts_per_mm
```

Increasing encoder count is positive. Motor polarity is `-1` on each tested axis.
Gains remain count-based; the saved bench profile is `Kp=.001`, `Ki=.001`, `Kd=0`.
The standard ROS joint state uses meters.

Open **Controller settings** on either axis to tune PID parameters while running.
Apply sends the complete parameter set together. Controller-type changes and
duty-limit edits require that axis disabled. **Save profile** writes the active
parameters to project YAML; **Save as new profile** creates another reusable
profile. Other axes retain their copies until **Load / Reload** is pressed.
Startup profile IDs, motor polarity, reference speed, tracking limit, SPI device,
counts/mm and travel bounds are configured in `projects/heart_phantom/bridge.yaml`.
UI profile selections and duty-limit edits are session-only; saving a profile
persists its parameters. See [profiles and controller development](../PROJECTS_GUIDE.md).

The position controller receives a reference ramp limited to **20 mm/s**,
**0.01 mm per 2 kHz cycle**. There is no acceleration/deceleration limit or
trapezoidal trajectory generator. Target, reference and measured position are
separate CSV signals; the controller panel reports the active slew rate.

The detached-shaft bench tests validate encoder-derived motion. They do not
measure actual rack displacement, backlash, or loaded tuning.

## Dashboard, system, and web settings

The fixed header shows **Heart Phantom** and keeps **Stop All** available on every
tab and while Settings is open. The left icon rail switches Dashboard, System,
and Resources; hover or keyboard focus shows tab names. Settings is at the bottom.
Dashboard stacks robot-view and robot-control placeholders, the DSP list,
two axis cards side by side on desktop, then recordings. Each axis card owns its
original full-width ruler slider, numeric target, and green actual-value marker.
The marker shows measured position in position mode and reported output duty in
PWM mode. System enable is beside the Axes heading. Resources separates axis
bindings/calibration, transport, and currently unavailable expansion I/O.

System refreshes aggregate CPU and individual CPU 0–3 usage, memory/disk usage
and capacity, temperature, and ROS graph information once per second by default.
Each core has its own meter. ISOLATED comes from the Linux CPU isolation list;
RT PINNED identifies CPU 3, the configured physical bridge worker affinity. Mock
mode has no pinned real-time worker. GPU usage shows unavailable if the driver
does not expose it. The bridge card distinguishes its RT worker from non-RT ROS/
recording work and reports measured deadline misses/max cycle work. Other nodes
have no invented per-node deadline counters. Expand a topic to open a 10 Hz latest-
message view; collapsing it or leaving System closes that subscription. Slow/event
publishers retain their last message and show its age/count.

Settings persist in that browser: light/dark/system theme, plot refresh 15/30/60 fps,
web state stream 10/25/50 Hz, host status refresh 1/2/5 s, and optional frontend
performance readout. These affect web presentation/delivery, not the 2 kHz control
loop, 50 Hz ROS state publication, or full-rate CSV recording.

## Direct PWM mode

Disable an axis, select **PWM / duty** on its axis card, then enable it. Each enable
starts at **zero duty** even if a value was entered while disabled. The direct
control slider/input now uses signed percent, bounded by the configured duty cap.
API/ROS duty remains normalized `[-1, +1]`. PWM bypasses PID and motor-sign
conversion; positive duty is the DSP's positive direction, which may decrease mm.
Releasing the slider or closing the browser **keeps that duty**. Set it to zero,
disable the axis, or use Stop All to stop it. There is no position hold in PWM mode.

Mode changes require the axis to be effectively disabled. They clear PWM and
initialize the position target/reference from the current measured position. A
PWM move outside configured position bounds latches a host fault and disarms both
axes; this software check is not physical homing/limit-switch functionality.

The software Stop All still depends on the host/network path and is not a hardware
E-stop. A physical E-stop remains separate.

## Recording

Start recording captures every valid received SPI frame at the 2 kHz host rate.
Stop & save finishes the file, then Download CSV retrieves it. Recordings are in
`/home/toby/Project-Crazy-Diamond/recordings/`; the UI lists the newest ten, while
`GET /api/recordings` lists all. There is no automatic deletion or duration cap.

The first CSV line records project ID, each axis's counts/mm, and physical/mock mode. Columns contain:

- One unwrapped DSP `timestamp_us` and host sample number.
- Each axis's target, slew reference, and measured position in mm.
- Raw/zero counts, DSP requested duty, DSP output duty, host enabled flag.
- Each axis's gains, duty limit, control mode (`0` position / `1` PWM), and
  normalized PWM target for that frame. Position targets are inactive in PWM mode.

The timestamp is DSP uptime, not wall-clock time. Full-duplex telemetry predates
the current command; a row is the latest combined host/DSP snapshot, without
per-signal timestamps or an invented same-tick acknowledgment. Output duty is a
software command, not measured voltage/current. Drops and writer errors appear
in the UI. Disk writes and CSV formatting run outside the real-time thread.
An abrupt process/power loss may lose the buffered tail of an active file.

## Build and run

The Pi uses Ubuntu 24.04.4, ROS 2 Jazzy, and the existing PREEMPT_RT kernel/CPU 3
isolation. The application runs from the Git checkout at
`/home/toby/Project-Crazy-Diamond`. Source updates now use Git; see
[the laptop/Pi workflow](GIT_WORKFLOW.md).

On a clean Ubuntu 24.04 system, configure the official
[ROS 2 Jazzy apt source](https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html),
then install:

```bash
sudo apt update
sudo apt install ros-jazzy-ros-base python3-colcon-common-extensions \
  build-essential cmake python3-fastapi python3-uvicorn python3-multipart \
  python3-pytest python3-httpx
```

Build the frontend with Node.js 22.12+ (or 20.19+) on a development machine or Pi:

```bash
cd web_ui
npm ci
npm test
npm run build
```

Pull the source on the Pi and build its frontend there. From the checkout root,
build the ROS packages and run their tests:

```bash
bash scripts/build_ros.sh
```

### Mock mode

First stop the physical services so there is only one ROS bridge/API:

```bash
sudo systemctl stop diamond-ui diamond-bridge
bash scripts/run_bridge.sh true
```

In a second shell from the same root:

```bash
bash scripts/run_ui.sh
```

The mock opens no SPI device and needs no RT privilege. It uses a simple
deterministic first-order plant to test workflows, not controller dynamics.
Run `python3 tests/integration.py` against it. Quit both processes before returning
to the physical services.

### Physical services

The checked-in unit files target user `toby` and `/home/toby/Project-Crazy-Diamond`.
Adjust those paths/user if deploying elsewhere. Install and start them:

```bash
sudo cp scripts/diamond-bridge.service scripts/diamond-ui.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now diamond-bridge diamond-ui
```

Bridge service limits allow SCHED_FIFO 80 and memory locking. Non-RT work uses
CPUs 0–2; the bridge thread moves to CPU 3. The web service uses a loopback-only
UDPv4 profile because shared-memory DDS stalled on this bench Pi.
Use the same profile for ROS CLI diagnostics:

```bash
source /opt/ros/jazzy/setup.bash
source ros2_ws/install/setup.bash
export ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT
export FASTRTPS_DEFAULT_PROFILES_FILE="$PWD/config/fastdds-loopback.xml"
ros2 topic list
```

## Checks and recovery

```bash
systemctl status diamond-bridge diamond-ui
journalctl -u diamond-bridge -u diamond-ui -n 80 --no-pager
```

The System tab reports ROS nodes/topics, kernel, load average, maximum SPI/control
work time, wake lateness, deadline misses, telemetry errors, and sample drops.
A startup pipeline-fill telemetry error can occur while disarmed; subsequent
invalid/stale frames fault the host. Physical faults, non-finite values, excessive
tracking error, and missed armed deadlines disarm. Clear host fault leaves motors
disabled; resolve the cause before re-enabling. A DSP reset can require a bridge
restart to establish the new timestamp epoch and startup zero.

Stop the bridge before running the original sine/probe tools. The shared SPI
driver takes an exclusive advisory lock; old binaries may not honor that lock.
Service restarts start disarmed; process death relies on the DSP's existing
50 ms watchdog until the replacement process sends DISARM.

The integration script moves real motors only with `--physical`:

```bash
python3 tests/integration.py --physical --pwm
python3 tests/integration.py --physical --sweep
```

The first test uses ±2 mm with a 25% duty cap and short ±3% direct-duty pulses. The second additionally exercises
both ±50 mm endpoints and records for over 60 seconds. Both leave motors disabled
and restore previous duty limits. Use the detached/unloaded bench configuration
for these tests. [Results](STAGE2_RESULTS.md) record what was actually verified.

CSV playback, physical switch homing, LQR implementation, multiple DSPs,
robot kinematics, and final visual polish are later work. Reusable PID profiles
and live tuning are implemented.

## Browser delivery regression check

From the development checkout root, run:

```bash
node web_ui/node_modules/vite/bin/vite.js --config tests/ui_stall_proxy.mjs
```

Open `http://127.0.0.1:8766/` in Chrome. This diagnostic proxy intentionally stalls
WebSocket delivery for one second every five seconds while leaving HTTP snapshots
available. Its overlay reports frame gaps, DOM-update gaps, and canvas allocation
counts. It connects to the real Pi API: ordinary motor controls still work, so
use a disabled axis to test sliders without motion. This diagnostic is not part
of the deployed UI. Set `DIAMOND_WS_STALL_MS=0` to measure without injected stalls.

## Firmware-v5 acceptance tests

Use `tests/integration.py --physical --pwm --sweep` on the Pi for the bounded
±50 mm motor/one-minute recorder regression. This moves the hardware and leaves
motors disabled. Use `tests/io_integration.py` with the bridge running and motors
already disabled for the I/O command/CSV path; it does not measure pin voltages.

For raw endpoint fault tests, stop `diamond-bridge`, compile
`tests/hardware_endpoint_test.cpp` with C++17 and run it on the Pi. It never arms
a motor, but sets general digital/DAC outputs and deliberately sends malformed
traffic / stops heartbeats. Add `--loopback` only after wiring the documented
DO→DI and DAC→ADC connections with matching board references. Restart the bridge
afterward. Original v2/v3 bench tools are incompatible with v5.

[JTAG/RAM/FLASH instructions](../../../../dsp/f28379d/README.md) and
[measured results](STAGE3_RESULTS.md) describe the firmware migration. A CCS
program verification does not replace the standalone power-cycle check.
