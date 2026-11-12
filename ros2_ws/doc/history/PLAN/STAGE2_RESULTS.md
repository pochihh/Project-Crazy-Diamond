> Historical snapshot. Current system documentation: [index](../../README.md).

# Stage-2 acceptance results

Date: 2026-11-10. **Stage 2 complete for the two-axis unloaded bench.**
Operating instructions: [STAGE2_OPERATION.md](STAGE2_OPERATION.md).

## Delivered

- ROS 2 Jazzy `bridge` with a dedicated 1 kHz SPI/control thread; unchanged
  command-v2/telemetry-v3 DSP firmware. Shared codec also serves the old bench tool.
- Python/FastAPI ROS bridge and TypeScript/Vite web control desk on port 8000.
- Independent two-axis calibrated mm sliders/number targets, live position/duty
  and plots, system/axis enable, Stop all, disabled-axis Reset zero, runtime tuning.
- Full received-frame-rate CSV recording with one timestamp, start/stop/download,
  sample/drop counts, and writer-error reporting.
- Resource and ROS/Linux status tabs. Initial grid/color/engineering-table styling
  follows the owner's UI references; detailed polish remains stage 4.
- Mock mode, automated tests, configuration, scripts, persistent systemd services,
  and build/operation documentation.

The deployment is `/home/toby/crazy-diamond-stage2` on `192.168.1.21`.
`diamond-bridge` and `diamond-ui` are active and enabled at boot. The original Pi
source tree was preserved; no DSP programming or kernel change was performed.
The final observed state had both motors disabled, both host zeros reset to the
current positions, no active recording, and no fault.

## Checks performed

| Check | Result |
|---|---|
| TypeScript check and production Vite build | Passed |
| Pi ROS build and colcon tests | 3 packages; 3 tests, 0 errors/failures/skips |
| C++ core | CRC known vector, command/frame encoding, bad CRC, calibration, zero interlocks, target bounds/NaN, outside-range enable, enable/stop/fault transitions, timestamp wrap, threaded SPSC ordering |
| Python API | Offline response, invalid axes/non-finite values, active-file protection, completed download, missing recording |
| Original motor bench self-test after codec extraction | Passed |
| Mock HTTP end-to-end | Passed; 3,172 consecutive recorded samples, no drops |
| Initial physical ±2 mm test | Passed; 3,641 consecutive recorded samples, no drops/deadline misses |
| Physical full-range test | Passed; details below |
| Browser controls | Both physical sliders, pointer drag on axis 0, live feedback, enable/stop, recording, disabled-axis reset |
| Browser disconnect/reconnect | Both axes held +3/−3 mm and recording continued; reopening restored active targets/enables |
| Browser download | Completed CSV downloaded and independently checked: 64,162 ordered consecutive samples, increasing timestamps |
| Bridge service restart | Starts physical and disarmed, captures new startup zero; no automatic re-arm |
| Final service state | Both active/enabled, restart counters zero |

The C++ tests simulate encoder movement before manual reset. The physical reset
test used a position reached through motor motion, then disabled/reset that axis;
no person was asked to manually move the shaft during this unattended run.

## Physical full-range run

Command: `python3 tests/integration.py --physical --sweep`.
Both motor shafts were detached as confirmed by the owner. Each test temporarily
limited duty to 25%, then restored the original 100% limit while leaving motors
disabled. Default gains and the 20 mm/s reference slew were used.

Both axes reached `[+50, −50]`, then `[−50, +50]`, then `[0, 0]` mm within
0.3 mm of target. The final ±1 mm hold, per-axis disable/reset, global stop,
recording finish, and CSV verification also passed. These are encoder-derived
positions, not measured rack travel. ±50 mm bounds the **command**, not actual
overshoot or a hardware limit switch.

| Metric | Acceptance result |
|---|---:|
| Recorded samples | 64,162 |
| DSP timestamp span | 64.159794 s |
| Missing/duplicate sample numbers | 0 |
| Telemetry errors | 0 |
| Dropped samples | 0 |
| Control deadline misses | 0 |
| Maximum SPI/control work | 226.964 µs |
| Maximum wake lateness | 22.368 µs |

The UI remained connected during the run, including ROS/system-status viewing.
A later final check at 478,824 received frames still showed zero telemetry
errors, drops, and deadline misses; maxima then were 310.169 µs work and
48.102 µs wake lateness. These are measured bench observations, not a worst-case
latency guarantee.

Recording: `session-1794350887969078723.csv` in the Pi deployment's `recordings/`.
The browser-downloaded copy is in the operator's Downloads folder. SHA-256:

```text
7d09b87544941ce0622a887f52bd11cf4a640fca7204d30df413201ea377a919
```

The separate browser-disconnect run produced 45,817 samples. The earlier physical
startup had one invalid pipeline-fill frame while disarmed; it did not recur in
the final acceptance run.

## Issues resolved during integration

- Fast DDS shared-memory transport stopped delivering state during initial mock
  testing. Both scripts now use UDPv4 with localhost discovery. Subsequent mock,
  physical, browser, and sustained recording tests used that configuration.
- Mock scheduling now reports lateness without pretending to have real-time
  privileges; physical motion still faults on an armed missed deadline.
- Mock motion includes the reset offset, so moving after manual reset uses the
  same coordinates as hardware.
- Stop overrides pending enables even if the command queue is full. Command
  consumption per real-time tick is bounded.
- Dirty target/tuning fields are preserved while the live UI updates.

## Limits and next work

No loaded-mechanism displacement/backlash or full mechanical travel validation
was performed. Positive position follows encoder counts; rack direction must be
checked after reconnecting the mechanism. No power-cycle, crash-tail durability,
forced disk failure, or physical E-stop/watchdog fault-injection test was run.
The current watchdog implementation was retained and inspected, not re-qualified.

The old working CSV-input source and exact loaded DSP firmware revision remain
unidentified. Stage 2 does not implement playback, switch homing, DIO/AIO, extra
encoders, new DSP controllers, 2 kHz host streaming, or robot kinematics.
Those remain in [stages 3/4](IMPLEMENTATION_PLAN.md).

The UI assumes a single trusted-LAN operator. Runtime tuning and manual zero are
not persisted across bridge restarts. CSV writing shares the non-RT ROS executor;
long disk stalls may delay commands/status and overflow the visible sample queue.
Only the newest ten recordings appear in the UI, while the API/filesystem retain
all sessions. There is no automatic disk cleanup.

## Follow-up: slider pause and plot smoothness

The owner reported an approximately one-second pause after releasing a slider
in Chrome. Diagnosis separated browser painting, web delivery, and physical
control:

- Pi-loopback WebSocket: 1,477 messages in 30 seconds, median interval 20.097 ms,
  worst 23.675 ms, no interval over 50 ms.
- The same stream over the LAN had a 1,016.903 ms gap. Longer monitoring also
  saw multi-second delivery gaps while Chrome continued painting. Turning off
  Pi Wi-Fi power saving did not eliminate the issue; the original setting was
  restored. The precise radio/TCP cause was not established.
- The old frontend also reallocated both canvas buffers and rebuilt a hidden
  resource table for every message. It drew graphs by sample index on packet
  arrival, making delivery jitter visible as uneven plot motion.

The fix is confined to TypeScript and the Python web adapter: display-paced
painting, retained canvas buffers, frame-time plot spacing with a 100 ms buffer,
coalesced DOM updates, visible-table updates at 5 Hz, one target submission per
input change, complete pointer-release cleanup, and bounded command waits.
When WebSocket updates stall, an independent HTTP snapshot restores fresh state;
web-process/sequence metadata prevents old queued packets from replacing it.
Missing samples remain gaps; no measured values are synthesized.

A diagnostic proxy injected one-second WebSocket stalls every five seconds:

| Measurement | Original UI (~28 s) | Fixed UI (~62 s, repeated drags) |
|---|---:|---:|
| Worst WebSocket gap | 1,053.4 ms | 1,071.6 ms |
| Worst live DOM-update gap | 1,054.4 ms | 283.6 ms |
| Worst animation-frame interval | 116.7 ms | 35.2 ms |
| Frames over 50 ms | 4 | 0 |
| Long tasks | 2 | 0 |
| Canvas dimension writes | 5,512 | 4 (startup only) |

Three frontend tests passed: retained buffers/no repeated layout reads, actual
frame-time spacing and missing-data gaps, and timeout/error recovery. Three Python
web tests passed, including snapshot sequencing across a host-controller restart.
TypeScript and production builds passed. Repeated slider input and pointer
drag/release passed in Chrome during the injected stalls. The reproducible proxy
is `tests/ui_stall_proxy.mjs`; it is excluded from the deployed frontend bundle.

Only `diamond-ui` restarted. `diamond-bridge` retained its original main PID
62163, startup time 14:47:56 PST, and restart count zero. At 3,261,244 physical
samples, deadline misses, telemetry errors, dropped samples, and host faults were
all zero. A concurrent 29-second check advanced 29,060 samples with the motor
enabled and no counter regression. Maximum work/wake lateness over that whole
controller run was then 462.835/65.699 µs. Zeros, gains, DSP firmware, and the
real-time executable were not changed.

These checks establish continuity during the observed tests, not immunity to
every network or hardware failure. If both HTTP and WebSocket delivery stall,
the UI shows delayed data; network loss cannot be hidden by a rendering change.


## Follow-up: Heart Phantom layout and host PWM mode

Implemented the 2026-11-10 layout/function feedback before protocol migration:
fixed project/Stop All header, icon rail/bottom Settings, dashboard scene/direct
controls/DSP/axis sections, duty progress meters, green enable switches, System
resource meters/topic streams, and a clearer Resources register. Project metadata,
calibration/bounds/tuning, and C++ controller selection now live in
`projects/heart_phantom/`. Advanced controller work starts in C++/configuration.

Host changes add per-axis position/PWM mode and project calibration parameters;
the DSP firmware and command-v2/telemetry-v3 frames were not changed. PWM is direct
signed normalized duty, with zero on enable, disabled-only mode switching, duty
caps, and host travel/fault checks. ROS state/CSV include mode and PWM target.
The new ROS message required rebuilding/restarting both host services; the physical
bridge started disarmed at 17:44:47 PST with new startup zero offsets.

Validation:

- Five frontend tests and the TypeScript/production build passed. Eight ROS/core/
  Python tests passed, covering PWM/limits/zero transitions, settings, optional
  host metrics, project selection, and topic/telemetry socket cleanup.
- Mock position/PWM/reset/recording integration passed with 4,181 consecutive CSV
  samples and zero telemetry errors/drops. Its ordinary scheduler recorded two
  deadline overruns; mock execution does not claim real-time scheduling.
- Physical ±2 mm position and ±3% PWM-command/recording integration passed:
  **4,861 consecutive samples**, CSV header/row agreement, monotonic frame times,
  and zero deadline misses, telemetry errors, or drops. Recording:
  `session-1794361502448682239.csv` in the Pi deployment.
- ±3% PWM was below the observed motion threshold. Additional bounded pulses
  verified actual encoder movement: axis 0 at ±10% changed −5 / +128 counts;
  axis 1 did not move at +10%, then ±20% changed −11,138 / +11,299 counts.
  These are unloaded bench observations, not a motor model or loaded tuning.
- Chrome verified all three tabs, live topic content, light/dark theme, persisted
  settings, 10/50 Hz web delivery selection, PWM labels/ranges, enabled-mode/reset
  interlocks, a 0.8 mm position command, and Stop All while Settings was open.
  Header/rail stayed visible while scrolling through the axis/recorder sections.
- Corrected selected-icon hover contrast and phase drift in the new plot-rate
  limiter. A desktop readout showed 60 display fps / 60 plot fps / 50 Hz data,
  maximum paint 4.1 ms for that one-second window. This is a spot check, not a
  claim that the previously reported intermittent LAN lag is resolved.
- At **397,923 physical frames**, faults, deadline misses, telemetry errors, and
  sample drops were all zero. Maximum cycle work / wake lateness were
  **232.260 / 23.644 µs**. Both services were active with zero automatic restarts.
  Final state: both axes disabled, position mode, zero PWM targets, original duty
  limits restored, recording stopped.

The robot picture remains a labeled placeholder. GPU utilization is unavailable
from this Pi's driver. Only the bridge has instrumented deadline counts; other
nodes show no fabricated values. Final styling, loaded mechanism validation, and
continued lag diagnosis remain open. The previous host source was preserved at
`/home/toby/.local/diamond-backups/before-ui-layout-20261110.tar.gz`.


## Follow-up: original sliders, axis-card placement, and per-core CPU status

Restored the original full-width ruler slider, larger rectangular thumb, green
actual-value marker, and separate numeric target row. Drag still sends current
targets and holds on release, retaining the existing coalescing/timeout fixes.
Each slider belongs to its axis card. The two axis cards share one desktop row;
the area beside the robot view is an explicitly labeled robot-control placeholder.
System enable moved to the Axes heading.

System now displays four separate CPU usage bars. The isolated-core list is read
from Linux; CPU 3 is tagged ISOLATED and RT PINNED for the physical bridge. On the
Pi, `/sys/devices/system/cpu/isolated` reported `3`, and the FIFO-80 worker TID
67115 had `Cpus_allowed_list: 3`.

The production build, five frontend checks, and eight Python API/metrics checks
passed. Chrome verified the two-column cards, original slider/marker behavior,
position/PWM ruler units, robot-control placeholder, and separate core meters/tags.
Slider drag/release was exercised with the motor disabled: the target held at
13.4 mm while the measured marker stayed at 0.80 mm, then the original mode/target
was restored. No motor-motion test was needed for the layout change.

Only the web adapter restarted to load the isolation metadata. The real-time
bridge retained main PID 67060, startup 17:44:47 PST, and zero automatic restarts;
deadline misses, telemetry errors, and dropped samples remained zero. The DSP
firmware, controller executable, startup zeros, and tuning were unchanged.
