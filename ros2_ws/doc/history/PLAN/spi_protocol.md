> Historical snapshot. Current system documentation: [index](../../README.md).

# SPI protocol v5

Updated: 2026-11-10. Implemented contract; hardware acceptance is recorded in
[stage-3 results](STAGE3_RESULTS.md). This replaces command v2 / telemetry v3.
The unimplemented v4 proposal and DSP A/B controllers are superseded.

## Ownership and timing

The Pi runs controllers and sends normalized motor duty. The DSP applies duty,
reads six encoders and general I/O, and enforces command validity, encoder
availability and timeout protection. Host reference counts are echoed for
recording; they are **not DSP position setpoints**. No DSP controller tuning,
homing, FOC, quaternion or auxiliary-encoder command exists in this build.

Pi: `/dev/spidev0.0`, Linux mode 1, 16-bit words, 10 MHz, 2 kHz target.
DSP: `SPI_PROT_POL1PHA1`, SPIA GPIO58/59/60/61; GPIO40 mirrors CE for XINT5.
The existing working mode pair is preserved. Each full-duplex transaction clocks
96 words / 192 bytes, nominally 153.6 µs before host/controller overhead.
CPU1 assembles a snapshot at 5 kHz. CPU2 publishes its newest three counts at 5 kHz.

One frame timestamp describes the latest available collection. ADC conversion
runs between CPU1 ticks; CPU2 counts come from a coherent double buffer. This is
not simultaneous physical sampling. There are no per-channel timestamps.
Telemetry was prepared before the command in that transaction arrived; use the
acknowledged command sequence rather than assuming zero-latency application.

## Encoding

Source of truth: [shared codec](../../../../dsp/common/protocol.h),
[endpoint state machine](../../../../dsp/common/endpoint.h),
[Pi transport](../../../../dsp/host/spi.hpp), [SPI driver](../../../../dsp/f28379d/cpu1/spi_dma.c).
No packed C structs cross the wire; C28x has 16-bit addressable characters.

- Each word is transmitted MSB first; 32-bit values use low word, then high word.
- Signed counts are two's-complement int32; duty is IEEE-754 binary32, −1 to +1.
- CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF, no reflection/XOR-out.
  Feed each word's high byte before its low byte.
- Reserved command words must be zero. Length/version/CRC must match exactly.
- Telemetry has four leading padding words; host accepts a shifted header only
  when the complete versioned payload and CRC fit inside the transaction.

### Pi → DSP

| Word(s) | Value |
|---|---|
| 0 | Header 0x55AA |
| 1 | Version 5 |
| 2 | Total words 96 |
| 3 | DISARM=0, ARM=1, CYCLE=2, CLEAR=3, SYNC=4 |
| 4–5 | Nonzero host session ID |
| 6–7 | Command sequence |
| 8 | Motor arm mask, bits 0–5 |
| 9 | Four digital output bits |
| 10–11 | Two DAC codes, each 0–4095 |
| 12–23 | Six requested motor duties |
| 24–35 | Six host reference counts, echoed only |
| 36–94 | Reserved zero |
| 95 | CRC over words 0–94 |

### DSP → Pi (nominal buffer offsets)

| Word(s) | Value |
|---|---|
| 0–3 | 0xFFFF pipeline padding |
| 4–6 | Header 0xAA55, version 5, total words 96 |
| 7–9 | Device 0x2837, firmware 0x0100, capabilities 0x000F (duty/encoder/DIO/AIO) |
| 10–11 | Snapshot timestamp, uint32 microseconds |
| 12–13 | Snapshot sequence, incremented at 5 kHz |
| 14–15 | Current host session |
| 16–17 | Last accepted command sequence |
| 18 | State: disarmed=0, active=1, fault=2 |
| 19–21 | Applied motor arm mask, fault bits, encoder-valid bits |
| 22–25 | Digital inputs, digital outputs, AI-valid bits, AO-valid bits |
| 26–31 | Six raw ADC results, 0–4095 |
| 32–33 | Two commanded DAC codes, 0–4095 |
| 34–45 | Six encoder counts |
| 46–57 | Six requested duty values |
| 58–69 | Six applied software duty commands |
| 70–81 | Six echoed host reference counts |
| 82–83 | Invalid frame count |
| 84–85 | Rejected command count |
| 86–87 | Watchdog trip count |
| 88–89 | Sum of encoder transition/error counters |
| 90 | Fixed wiring configuration ID 1 |
| 91 | CPU2 maximum observed polling gap, rounded up to microseconds |
| 92 | CRC over words 4–91 |
| 93–95 | Zero padding |

Duty and DAC fields are software commands, not measured voltage/current/torque.
Encoder validity indicates a functioning acquisition path, not proof that an
external encoder is plugged in. ADC validity indicates a completed acquisition,
not a calibrated voltage or verified external connection.

## State and recovery

- Boot: motor PWM zero, all STBY low, shared EN low, DO low, DAC code zero.
- SYNC establishes a new host session and always removes drive/output commands.
  It does not clear a fault. Same-session sequences must advance; a replayed
  SYNC cannot rewind the sequence anchor.
- ARM accepts only available motor encoders, clears all duties and I/O outputs,
  and enables cyclic output updates. Mask zero permits I/O-only operation.
- CYCLE requires the current session, advancing sequence, active state and the
  same arm mask. Changing selected axes requires another ARM.
- DISARM removes outputs even with a stale/foreign session. Such a command is
  not acknowledged as accepted unless session/sequence validation succeeds.
- CLEAR requires disarmed state; it clears the fault and leaves outputs off.
- More than 50 ms without an accepted update while active latches timeout bit 0.
  Duplicate, invalid and foreign-session commands do not refresh the watchdog.
- Invalid command semantics/non-finite duty latch bit 1. Loss of availability
  for an armed encoder latches bit 2. All faults zero motor/DO/DAC outputs.
- CRC/version/length errors are rejected without changing outputs or refreshing
  the watchdog. Commands have to recover before the watchdog expires.
- Timestamp and sequence comparisons use modular 32-bit arithmetic.

The host starts disarmed, checks advancing telemetry and command acknowledgments,
and stops on telemetry/controller/active motor deadline faults. Browser loss
continues to hold the last target under the accepted policy; host loss is handled
by the DSP watchdog. Reset zero is a host offset change on a disabled axis.
Automatic homing and limit-input roles remain deferred; general DI is not yet a
local limit-protection implementation. The physical E-stop remains external.

## Checks and compatibility

`tests/protocol_test.cpp` checks independent CRC/golden packing, bit corruption,
reserved words, session/sequence rules, timeout/clear and quadrature decoding.
`tests/hardware_endpoint_test.cpp` exercises malformed traffic and watchdogs on
real SPI with all motor bits zero; optional `--loopback` requires the documented
physical wiring. `tests/integration.py` preserves the two-axis UI/API regression.

The legacy bench executable uses `dsp/host/legacy_spi.hpp` and cannot control a
v5 image. Restore its matching firmware for recovery; there is no automatic
fallback between protocols. The ROS bridge/UI/recorder deploy together with v5.
