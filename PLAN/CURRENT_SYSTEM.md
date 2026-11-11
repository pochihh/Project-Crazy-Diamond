# Current system

Updated: 2026-11-11. The v5 migration passed the agreed bench acceptance; consult
[stage-3 results](STAGE3_RESULTS.md) for measured evidence and coverage limits.
The original two-axis CSV result and [stage-2 tests](STAGE2_RESULTS.md) are the
preserved baseline, not evidence that all six physical axes have been tested.

## Active path

```text
Browser target (mm) → Python / ROS command → Pi reference slew (20 mm/s)
 → Pi C++ position PI/PID → normalized duty over SPI
 → DSP CPU1 5 kHz endpoint → 20 kHz PWM / DIR / STBY / shared EN → motor
 ← one latest-value frame ← three eQEP encoders + three CPU2 GPIO encoders + I/O
```

The browser also offers explicit direct PWM mode. Release holds the last target;
browser disconnect does not disarm. Stop All acts through PCD regardless of the
robot placeholder panel. Startup captures each axis's zero once. Reset zero
requires that axis disabled; disable/enable and browser reconnect preserve zero.
Automatic homing is deferred.

| Component | Implemented behavior |
|---|---|
| Pi host | ROS 2 Jazzy C++ bridge; dedicated 2 kHz control/SPI thread |
| DSP CPU1 | SPI DMA, three eQEPs, six duty outputs, 5 kHz snapshots, watchdog and faults |
| DSP CPU2 | Dedicated GPIO x4 quadrature polling for axes 3–5; coherent IPC snapshots |
| DIO/AIO | 4 DI, 4 DO, 6 ADC inputs, 2 DAC outputs; raw analog codes |
| Web UI | Python/FastAPI and TypeScript/Vite; two calibrated axis cards, robot placeholders, resources and settings |
| Recorder | Every received control-loop sample to downloadable CSV, writing outside RT; six counts and I/O included |
| Controllers | Compiled PID registry, project YAML profiles and live parameter tuning; LQR model pending |
| Robot packages | External node/panel integration remains [proposed](ROBOT_AND_CONTROLLERS.md) |

Controllers remain on the Pi. The previous CLA PID files are retained for history
but excluded from the active build. The DSP is not running PID/LQR/A/B feedback
controllers. Auxiliary encoders and automatic home/limit handling are not active.

## Calibration and control

Both working axes use a 500-cycle encoder, ×4 decoding, 91/6 reduction, and a
30-tooth module-1 rack/pinion. Nominal scale is **321.846662696944 counts/mm**;
travel range is **−50 to +50 mm** relative to startup/manual zero.
The saved bench profile is Kp=0.001, Ki=0.001, Kd=0, motor sign −1, maximum duty 1.0.
Gains act on native encoder-count error. The PID integration uses the actual host
sample period; changing display units does not rescale gains.

Position commands pass through a reference slew limit of **20 mm/s**, or
**0.01 mm per 2 kHz tick**, before PID feedback. A step target therefore becomes
a linear reference ramp. This is a maximum reference advance, not a trapezoidal
trajectory: acceleration/deceleration are not limited. CSV records target,
reference and measured position separately. Direct PWM bypasses this ramp.

The hardware exposes six encoder/duty channels; the current robot project and
axis controls use axes 0/1. Resources and CSV expose all six raw encoder counts.
Unwired channels are not evidence of successful physical encoder validation.

## Transport and I/O

[Protocol v5](spi_protocol.md) defines a single shared host/DSP codec: 96 16-bit
words / 192 bytes per full-duplex exchange at 10 MHz. Linux uses SPI mode 1;
DSP uses the preserved `SPI_PROT_POL1PHA1` configuration. SPI is GPIO58–61 with
GPIO40/XINT5 as CE mirror. Each snapshot has one timestamp, frame/command
sequences, confirmed arm/fault state, six encoders, references, duties and I/O.

CPU1 takes newest eQEP values; CPU2 publishes coherent count snapshots every
200 µs. ADC conversion runs between ticks. This is one consistent latest-value
frame, not simultaneous acquisition. ADC/DAC voltage conversion awaits confirmed
board reference selection. See [dock wiring](wiring_table.md) for pins.

Boot, Stop All, disarm and faults clear motor duty, DO and DAC commands. A valid
ARM with motor mask zero enables only general I/O. A 50 ms host command timeout
latches a fault. Explicit clear never arms outputs. Malformed/stale frames do
not refresh the watchdog. General digital inputs do not yet implement limit
interlocks; physical E-stop state is not reported by firmware.

## Deployment and verification

The Pi runs Ubuntu 24.04.4 LTS, PREEMPT_RT kernel `6.8.0-2049-raspi-realtime`,
ROS 2 Jazzy and the Git checkout in `/home/toby/Project-Crazy-Diamond`. Services are
`diamond-bridge` and `diamond-ui`; physical bridge startup is disarmed.
CPU 3 is isolated with `isolcpus=managed,domain,3`, `nohz_full=3`, `rcu_nocbs=3`.
The control thread is pinned to CPU 3 at SCHED_FIFO priority 80. ROS, web and
recording run on CPUs 0–2. UI resources list all cores and identify this core.
Laptop and Pi synchronize source through the same GitHub `main` branch; see
[Git workflow](GIT_WORKFLOW.md). The former `crazy-diamond-stage2` directory is
retained for historical results and is no longer the service working directory.
Detected host overruns and missed 500 µs periods are persistently logged with
timings, preceding-cycle context and raw SPI frames. See
[real-time diagnostics and measured coverage](REALTIME_STABILITY.md).

The bridge launcher uses `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` with
`ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`. The web launcher uses
`ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT` plus the explicit
`config/fastdds-loopback.xml` profile, which disables built-in transports and
binds its UDP transport to loopback. This web profile was introduced after DDS
shared-memory delivery stalls. External robot nodes will need an explicit
discovery/transport configuration change when robot integration is implemented.

Build DSP with `python3 scripts/build_dsp.py RAM` or `FLASH` using the installed
TI CGT 22.6.2.LTS / C2000Ware 6.00.01.00. Both cores are required.
The legacy bench test remains a recovery tool for the old firmware, not a v5
control client. Sources, old build artifacts, Pi checkout and both flash banks
were backed up before programming; see [bring-up record](STAGE3_RESULTS.md).

[Operating instructions](STAGE2_OPERATION.md) explain startup, manual motion,
recording and tests. [Roadmap](IMPLEMENTATION_PLAN.md) separates firmware
acceptance from controller profiles, external robot packages and UI refinement.
