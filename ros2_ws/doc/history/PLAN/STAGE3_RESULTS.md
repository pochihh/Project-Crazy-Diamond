> Historical snapshot. Current system documentation: [index](../../README.md).

# DSP v5 bring-up and acceptance

Updated: 2026-11-11. **Complete for the agreed bench scope:** RAM and FLASH
two-axis/protection acceptance, standalone power-cycle boot, all four digital
loopbacks, both DACs into all six analog inputs, and CPU2 functional encoder
counting pass. Both FLASH images are programmed and verified; the bridge and
web UI are restored, with motors and general outputs disabled. Full-speed
external CPU2 encoder testing is deferred. See the evidence and coverage limits
below rather than assuming all six physical axes are validated.

## Preserved baseline

Before changing firmware, Stop All was acknowledged and the Pi bridge stopped.
The baseline had sample 5,876,463, zero deadline misses, telemetry errors and
sample drops. Axis 0 was holding −3.9 mm; axis 1 was disabled.

- Local original DSP sources, target config, RAM/FLASH artifacts and host codec:
  `.local/firmware-baseline/local-dsp-before.tar.gz`.
  SHA-256 `31426c05b2974bfb0556bf732339c9fa760e7e979ae3dcb03bf6b03819f3dc83`.
- Pi backup: `/home/toby/.local/diamond-backups/before-firmware-migration-20261110.tar.gz`.
- Both original CPU flash banks were read back through CCS, each in eight
  64 KiB chunks: `CPU1-flash-*.bin`, `CPU2-flash-*.bin` under the same local
  evidence directory (1 MiB total). Hashes: `readback-sha256.json`.
- Existing original CPU1 RAM rebuild passed, SHA-256
  `4435a686dc6ee3430a5f8cdda7a7c6fc327f7b06015d9d7d8294fb7a48f03347`.

These ignored local artifacts contain the recovery material. The original flash
image's exact source revision remains unidentified; the readbacks preserve what
was actually programmed. Legacy host and v5 host require matching firmware.

## Implemented scope

- Shared protocol-v5 codec: 192-byte frames, session/sequence acknowledgment,
  identity/configuration, six count/reference/duty channels, 4 DI / 4 DO / 6 AI /
  2 AO, validity and diagnostics. Controllers remain on the Pi.
- CPU1 5 kHz snapshot/output/protection tick; three hardware eQEPs.
- CPU2 dedicated x4 GPIO quadrature polling; coherent double-buffered IPC,
  liveness checking and maximum polling-gap telemetry. Auxiliary encoders deferred.
- 50 ms latched watchdog, invalid-command and unavailable-encoder faults;
  disarm/stop/fault clear motor, digital and DAC output commands.
- Host upgraded to 2 kHz with corresponding PID sample period, new ROS state,
  six raw counts and I/O in CSV, and Resources I/O controls. Existing two-axis
  calibrated sliders, PWM mode, browser-disconnect hold and Reset zero remain.
- Both cores have RAM/FLASH linker/build configurations and reproducible build /
  CCS load-and-verify scripts. CLA controller files are excluded from active builds.

Robot packages and PID/LQR/YAML profiles are [documented](ROBOT_AND_CONTROLLERS.md)
for discussion, not implemented in this pass. Automatic homing is deferred.

## Build and software checks

TI CCS `/Applications/ti/ccs2020/ccs`, CGT 22.6.2.LTS, C2000Ware 6.00.01.00.
Both cores build for RAM and FLASH. Host colcon build passes all three packages;
10 tests pass. Web production build and five browser-side unit tests pass.

Shared protocol checks cover independent CRC and packing, every command-word bit
flip, reserved fields, state/session/sequence/watchdog/clear behavior, timestamp
wrap, and x4 quadrature. Host checks cover calibration/zero, controller modes,
I/O bounds/stop, faults, recording queue ordering and HTTP channel validation.

Mock v5 end-to-end checks also passed in isolated ROS domain 77 / port 18765,
without opening SPI: 8,399 position/PWM recording samples and 723 I/O recording
samples, no telemetry errors or drops. Four mock scheduling deadlines were missed
under ordinary Linux scheduling; these are diagnostic, not physical RT acceptance.
The temporary processes were stopped afterward. Evidence files:
`mock-integration-result.json`, `mock-io-result.json` in the local evidence directory.

A source/build snapshot containing 128 files (including all four RAM/FLASH images,
project/build configuration and a per-file SHA-256 manifest) is preserved as
`.local/firmware-baseline/v5-validated-20261110.tar.gz`; the adjacent `.sha256`
file checks the archive. This supplements the original pre-migration backups.
The final accepted snapshot is `.local/firmware-baseline/v5-accepted-20261111.tar.gz`
with its own `.sha256` file and per-file manifest. It adds the final test source,
updated documentation and November 11 hardware evidence; the four firmware
images are unchanged from the hashes below.

## Hardware findings and fixes

1. Initial ICEPick −2131 / scan-path stuck-at-ones was resolved after the owner
   enabled controlCARD A:SW1-1 and power-cycled. See
   [TI controlCARD guide](https://www.ti.com/lit/ug/sprui76/sprui76.pdf).
2. The new SPI rearm code originally cleared DMA trigger flags **after** FIFO
   reset, deleting the transmit priming event. RX commands were accepted, but
   TX underflow returned a delayed MOSI echo. Clearing flags before resetting
   the FIFO restores telemetry. No new motor command was enabled while this
   failure was present. The [TI DMA API](https://software-dl.ti.com/C2000/docs/C2000_driverlib_api_guide/f2837xd/build/html/modules/dma.html)
   describes trigger clearing and channel behavior.
3. TI default GEL clears RAM/resets on debugger connect. Diagnostic reconnects
   therefore cannot inspect a running RAM image without disturbing it.
4. Loading CPU1 after CPU2 resets CPU2. The reproducible loader loads CPU1 first,
   CPU2 last, verifies both images and checks all six acquisition paths are live.
5. Standalone CPU2 boot requests retry BootROM readiness without blocking CPU1's
   I/O tick. The owner's subsequent power cycle verified both cores boot from
   FLASH without the debugger; see the standalone results below.

## RAM acceptance

First isolate the firmware change at the original 1 kHz rate: physical ±2 mm
position/hold/Reset/PWM/CSV regression passed, 4,901 recorded samples, zero
telemetry errors, deadline misses and drops. Maximum host cycle 297.52 µs;
maximum lateness 19.101 µs.

Then run at 2 kHz with both cores loaded and verified:

| Check | Result |
|---|---|
| Both axes ±50 mm sweep and return | Pass, each within 0.3 mm when sampled |
| Hold without browser refresh | Pass |
| Disabled-only Reset zero and rejected out-of-range target | Pass |
| ±3% PWM command path | Pass; these short low-duty pulses did not produce measurable motion |
| CSV | 130,282 consecutive samples; strictly increasing frame timestamps |
| Host maximum cycle / lateness | 292.02 µs / 16.21 µs |
| Deadline misses / sample drops | 0 / 0 |
| Telemetry errors during regression | 0; one initial empty SPI pipeline frame before testing |
| CPU1 maximum tick time at load | 51 µs of a 200 µs period |
| CPU2 maximum observed polling gap at load | 207 cycles = 1.035 µs; telemetry rounds up to 2 µs |
| Encoder path validity | 0x3F (all six acquisition paths live) |

Result: `.local/firmware-baseline/ram-2khz-result.json`.
Pi recording: `session-1794369756053290168.csv`.
An earlier 2 kHz run was interrupted by test orchestration stopping the bridge;
it is not counted as acceptance. It had no control/telemetry/drop faults before
interruption. The complete repeat above is the acceptance result.

I/O-only SPI tests (all motor mask bits zero) passed CRC rejection/recovery,
short/long frame recovery, duplicate-command timeout, silent-host timeout,
latched-fault re-arm rejection, explicit clear and output-zero behavior.
These checks verify protocol/firmware command state; they do not measure pin
voltages or unwired ADC input accuracy.

The deployed Resources page was inspected in a browser: v5, 2,000 Hz, six raw
counts, validity/polling diagnostics, four digital and six analog inputs, four
DO switches and two DAC fields render with the existing layout. Full visual
polish and the previously reported intermittent network/UI lag remain separate.

## FLASH acceptance

Both CPU images were programmed and verified through CCS, then run together.
CPU1 maximum tick at load was 62 µs / 200 µs; CPU2 maximum poll gap was
208 cycles (1.04 µs, displayed as 2 µs); all six acquisition paths were live.

- Same physical ±50 mm sweep/hold/Reset/PWM regression: **PASS**.
- CSV: **130,132** consecutive samples, file `session-1794370053122663540.csv`.
- Host maximum cycle **293.945 µs**, maximum lateness **16.407 µs**.
- **Zero** telemetry errors, deadline misses and dropped samples.
- Endpoint protection tests: PASS, including short frames and 97/112-word
  transfers with a valid CRC in the nominal 96-word prefix; these must be rejected.
- I/O-only HTTP → ROS → SPI → CSV test: PASS for all four DO bits, both DAC
  commands, disabled-system rejection, zero motor arm mask, stop-to-zero outputs
  and six recorded counts. CSV **715** samples,
  `session-1794370149349261710.csv`. This is not a physical loopback measurement.

Local evidence: `flash-load.log`, `flash-2khz-result.json`,
`flash-endpoint-result.log`, `flash-io-api-result.json` under
`.local/firmware-baseline/`. Both motors and all I/O outputs were disabled after
testing, and the bridge was stopped for the subsequent standalone power cycle.

## Firmware artifacts

SHA-256 of tested RAM / programmed-and-verified FLASH images:

| Image | SHA-256 |
|---|---|
| CPU1 RAM | `9eadb8f4556effa28275a7c43e01dc4cf12005b77c7bd20623077cc3b84832ac` |
| CPU2 RAM | `a6bb44e46adcd14e9b5f0f369a1760eccf56275938f64b00fb4da2db5cc7ea6f` |
| CPU1 FLASH | `da1de24e726d53350af03cff62c42d27bfa75de12487136a3f501fbc971beff9` |
| CPU2 FLASH | `5afc072fcedbb0c754a4ab140871df6dd590fa78bf9b78f77af66e01adbccaa9` |

## Standalone boot and first wired I/O check — 2026-11-11

The owner confirmed the DSP power cycle with JTAG disabled / flash boot selected,
and connected the requested loopbacks. No debugger connect/load was used during
this check. SPI returned valid v5 telemetry at uptime 79,410,124 µs, fault zero,
encoder validity **0x3F**, and CPU2 maximum poll gap **2 µs**. This verifies both
cores started from FLASH without debugger assistance.

Analog loopbacks respond at three levels (raw codes):

| DAC0 command | AI0 measured | DAC1 command | AI1 measured |
|---:|---:|---:|---:|
| 512 | 503 | 3583 | 3582 |
| 2048 | 2028 | 2047 | 2044 |
| 3584 | 3557 | 511 | 509 |

Maximum error is 27 counts (0.66% of full scale), within this functional test's
100-count tolerance. This checks DACA→ADCB0 and DACB→ADCB1; it is not an absolute
voltage calibration. The owner reports both SW2 switches appear right (nominal
3.0 V). Matched loopback ratios do not independently prove absolute Vref, so the
UI retains raw codes. AI2–5 had not yet received a known external stimulus at
this point; their subsequent six-input test is recorded below.

All 16 digital patterns were collected. DO0→DI0 matches. DO1 changes DI2 and DO2
changes DI1; DI3 remains high regardless of DO3. The configured pins match the
TI pinout, so the owner was asked to check 122→87, 123→147 and 124→148 before any
firmware change. The requested 121→85 path already works. The full digital
loopback test correctly reports **FAIL** until all four paths match.

Malformed-frame/watchdog/clear checks still passed, and the probe finished
disarmed. Evidence: `.local/firmware-baseline/standalone-loopback-20261111.log`.

### Corrected digital wiring

After the owner corrected 122→87 / 123→147 and replaced the 124→148 cable,
**all 16 commanded digital patterns matched all four physical inputs**. No
firmware change was required. Both DAC/ADC loopbacks and malformed-frame /
watchdog / clear tests also passed. Analog repeat readings were 504/3585,
2028/2043, 3560/505 for DAC commands 512/3583, 2048/2047, 3584/511.
Evidence: `.local/firmware-baseline/loopback-corrected-20261111.log`.

No encoders are attached to CPU2. The owner selected temporary test jumpers for
functional counting validation before a later full-speed encoder test. The test
uses separate DO0/1/2 A signals and shared DO3 B, checking each channel separately
and all three together in both directions. This does not establish the maximum
supported external encoder edge rate. Existing eQEP axis 2 and physical motors
2–5 remain outside the two-axis motor regression coverage.

### Six analog inputs and CPU2 functional counting

The owner installed the [temporary test wiring](wiring_table.md#temporary-cpu2-counting-test).
With the bridge stopped and motor arm mask zero, the endpoint test passed all
six analog input paths at three levels:

| DAC0 / DAC1 command | AI0 | AI1 | AI2 | AI3 | AI4 | AI5 |
|---|---:|---:|---:|---:|---:|---:|
| 512 / 3583 | 506 | 3584 | 513 | 3584 | 505 | 3587 |
| 2048 / 2047 | 2032 | 2047 | 2028 | 2045 | 2026 | 2047 |
| 3584 / 511 | 3551 | 508 | 3554 | 505 | 3552 | 507 |

AI0/2/4 follow DAC0 and AI1/3/5 follow DAC1. Maximum absolute difference is
33 counts (0.81% of full scale), within the functional tolerance of 100 counts.
This establishes signal routing and response, not absolute voltage calibration.

Each CPU2 encoder received 128 quadrature cycles per direction, independently
and then with all three channels active. Selected channels changed by exactly
**+512 / −512 counts**; unselected channels had zero net change. All eight cases
passed with **zero new transition errors**, validity **0x3F** and reported maximum
polling gap **2 µs**. Each case took 0.675–0.681 seconds (about 750 transitions/s
per selected channel), including host pacing and final settling. This is a
functional pin/decoder/IPC test, not a maximum-rate test. Malformed-frame,
watchdog, zero-output and explicit-clear checks also passed; the probe finished
disarmed. Evidence: `.local/firmware-baseline/encoders-analog-20261111.log`.

### Restored UI/host after standalone boot

After restarting `diamond-bridge`, the physical web-API regression passed for
both axes: bounded ±2 mm position control, hold, disabled-axis Reset zero,
rejected out-of-range commands, short ±3% direct-duty commands, and CSV. The
earlier FLASH ±50 mm / 60-second run remains the full-travel acceptance result;
no firmware changed between these runs.

- CSV: **9,567 consecutive samples**, `session-1794424490131029604.csv`.
- Maximum host cycle **284.741 µs**, maximum lateness **14.697 µs**.
- **Zero** telemetry errors, deadline misses and dropped samples.
- Bridge and UI services running; motors, DO and DAC outputs disabled afterward.
- Evidence: `.local/firmware-baseline/standalone-regression-20261111.json`.

## Coverage limits and follow-up

- Full-speed external CPU2 encoder validation is deferred by the agreed test
  approach. Functional jumpers do not certify maximum encoder speed.
- Existing eQEP axis 2 and physical motors 2–5 are outside the two-axis motor
  regression coverage. Mechanical loading and calibrated rack travel remain
  outside the detached-shaft bench test.
- SW2 / nominal 3.0 V is owner-reported; absolute reference voltage and analog
  accuracy are not calibrated. UI and recordings retain raw analog codes.
- Temporary encoder/analog fan-outs remain connected. Remove the encoder test
  jumpers before connecting actual encoders; DO changes currently stimulate
  those three encoder inputs. The earlier DO→DI loopbacks were replaced for
  this test, so the digital inputs no longer mirror the output commands.
- Robot packages, reusable controller profiles and online parameter tuning
  remain documented proposals, not implemented features of this firmware pass.
