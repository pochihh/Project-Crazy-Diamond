# SPI DMA Protocol v4

Status: proposed wire contract; freeze after host/DSP golden-vector and logic-analyzer tests.

Last updated: 2026-08-05

This is the authoritative cyclic protocol between a Raspberry Pi 5 master and a
TMS320F28379D SPIA slave. It replaces the incompatible v2/v3 frame definitions
currently present in the DSP firmware.

## 1. Transport contract

| Property | Value |
|---|---|
| Master / slave | Raspberry Pi master; DSP SPIA slave |
| DSP snapshot/control rate | 5,000 ticks/s, independent of SPI transactions |
| Transaction rate | 1,000 transactions/s baseline, initiated by the Pi; a later one-DSP 5,000 transactions/s mode uses the same frame after timing validation |
| Clock | 10 MHz target |
| SPI mode | Mode 3 (`CPOL=1`, `CPHA=1`), matching C2000 `SPI_PROT_POL1PHA1`; verify on both ends before freeze |
| Chip select | Active-low CE0; also mirrored to DSP GPIO40 for frame boundaries |
| Frame size | Exactly 80 × 16-bit words (160 bytes) in each direction |
| Bit order | Most-significant bit first within every 16-bit word |
| 32-bit word order | Low 16-bit word first, then high 16-bit word |
| Float format | IEEE-754 binary32 bit pattern, split low word then high word |
| Unused fields | Transmit as zero; receiver ignores only fields marked reserved |
| CRC | CRC-16/CCITT-FALSE over specified 16-bit words |

At 10 MHz, one 1,280-bit transaction takes 128 µs. This is 12.8% of the baseline
1 ms stream period and 64% of a 200 µs period at 5 kHz. The Pi, as master,
chooses the transaction cadence, but 5 kHz leaves only 72 µs for DSP DMA re-arm,
host scheduling jitter, and other link overhead. It is therefore a measured
later operating mode, not a first-pass guarantee; a validated 20 MHz clock would
reduce wire time to 64 µs if more margin is required. The first four
DSP-to-Pi words are pipeline padding until an on-hardware test proves they can
be removed; v4 keeps them so both frame sizes remain fixed.

CRC parameters:

- Polynomial `0x1021`
- Initial value `0xFFFF`
- `RefIn=false`, `RefOut=false`
- Final XOR `0x0000`
- Feed each 16-bit word's high byte, then low byte

## 2. Timing and ownership

```text
DSP, every 200 µs    snapshot ── CLA control ── apply PWM
Pi, every 1 ms       CS low ── clocks 80 words ── CS high   (baseline)
DSP SPI/DMA          already armed              validate/swap/re-arm
DSP command          apply at next 200 µs control boundary
DSP telemetry        latest coherent completed 5 kHz snapshot prepared for TX
```

Only the Pi starts transactions. A command becomes eligible at the next DSP
control boundary after its frame passes size, header, version, target-ID,
sequence, payload, and CRC validation. Telemetry is a coherent snapshot, not a
mixture of fields from different control ticks.

The command watchdog expires 50 ms after the last fully valid frame. A frame
with bad CRC, length, version, target ID, or sequence does not refresh it.

## 3. Common values

### 3.1 Command types

| Value | Name | Purpose |
|---:|---|---|
| `0x0001` | `CONTROL` | Cyclic mode and setpoint command |
| `0x0002` | `SET_RAW_AXIS_LIMITS` | Configure one axis in DSP-native units |
| `0x0003` | `SET_CONTROLLER_COEFFICIENTS` | Stage the A/B pair for one position or velocity controller |
| `0x0004` | `RESET_CONTROLLER_STATE` | Clear integrator/filter history |
| `0x0005` | `ARM` | Request transition from disarmed to armed |
| `0x0006` | `DISARM` | Request the safe disarmed state |
| `0x0007` | `CLEAR_FAULT` | Clear latched software faults when safe |
| `0x0008` | `ZERO_ENCODER` | Set selected native encoder counts to zero while disarmed |
| `0x0009` | `IDENTIFY` | Request an immediate status response for discovery/testing |
| `0x000A` | `COMMIT_CONFIGURATION` | Atomically activate a complete staged configuration revision |
| `0x000B` | `SET_AUX_ENCODER_CONFIG` | Configure one CPU2 encoder-only sensor in DSP-native units |
| `0x000C` | `SET_HOMING_CONFIG` | Stage one axis's raw-unit homing parameters |
| `0x000D` | `START_HOMING` | Start the configured one-axis homing sequence |
| `0x000E` | `ABORT_HOMING` | Abort homing and disarm all outputs |
| `0x000F` | `SOFTWARE_ESTOP` | Latch PWM zero, all STBY low, and shared EN low |

Value zero and all unlisted values are invalid in v4.

### 3.2 Axis modes

| 2-bit value | Mode | Setpoint representation |
|---:|---|---|
| 0 | `DISABLED` | ignored |
| 1 | `POSITION` | signed 32-bit encoder counts |
| 2 | `VELOCITY` | IEEE-754 float counts/second |
| 3 | `DUTY` | IEEE-754 float normalized to `[-1.0, +1.0]` |

### 3.3 Result codes

| Value | Name |
|---:|---|
| 0 | `OK` |
| 1 | `ERR_UNSUPPORTED_TYPE` |
| 2 | `ERR_INVALID_PAYLOAD` |
| 3 | `ERR_INVALID_STATE` |
| 4 | `ERR_OUT_OF_RANGE` |
| 5 | `ERR_STALE_SEQUENCE` |
| 6 | `ERR_CONFIG_REVISION` |
| 7 | `ERR_INTERNAL` |
| 8 | `ERR_INCOMPLETE_CONFIG` |

Header, length, version, target-ID, and CRC failures are counted but do not
produce a command acknowledgement because their sequence cannot be trusted.

### 3.4 Safety states

| Value | State |
|---:|---|
| 0 | `BOOT` |
| 1 | `DISARMED` |
| 2 | `ARMED` |
| 3 | `FAULT` |
| 4 | `HOMING` |
| 5 | `ESTOP` |

## 4. Pi-to-DSP frame

| Word(s) | Type | Field |
|---:|---|---|
| 0 | `uint16` | Header `0x55AA` |
| 1 | `uint16` | Protocol version `4` |
| 2–3 | `uint32` | Target device ID |
| 4–5 | `uint32` | Command sequence |
| 6 | `uint16` | Command type |
| 7 | `uint16` | Common flags; bit 0 = acknowledgement requested, others zero |
| 8–78 | 71 words | Type-specific payload; all unused words zero |
| 79 | `uint16` | CRC over words 0–78 |

The target device ID prevents applying a frame intended for a different DSP.
Command sequences increase modulo `2^32`; a repeated or older sequence is
rejected except after a DSP boot/session reset.

### 4.1 `CONTROL`

| Payload word(s) | Field |
|---:|---|
| 8 | Axis-enable mask; bits 0–5 correspond to axes 0–5 |
| 9 | Six packed 2-bit axis modes; axis 0 begins at bit 0 |
| 10–11 | Reserved, zero |
| 12–23 | Six 32-bit setpoints, axes 0–5 |
| 24–78 | Reserved, zero |

Interpret each setpoint according to its packed axis mode. A mode transition is
accepted only through a valid `CONTROL` frame and resets that axis controller
state before the new command is applied. An axis is enabled only when its mask
bit is set and its mode is not `DISABLED`. Enabling an axis outside the mask
accepted by the most recent `ARM` is rejected.

`ARM` itself never applies an earlier setpoint. Outputs remain disabled until a
valid `CONTROL` sequence newer than the accepted `ARM` arrives.

### 4.2 `SET_RAW_AXIS_LIMITS`

| Payload word(s) | Type | Field |
|---:|---|---|
| 8 | `uint16` | Axis index 0–5 |
| 9 | `int16` | Motor polarity, exactly `-1` or `+1` |
| 10 | — | Reserved, zero; current feedback is disabled in this PCB profile |
| 11 | — | Reserved, zero |
| 12–13 | `int32` | Minimum native position count |
| 14–15 | `int32` | Maximum native position count |
| 16–17 | `float32` | Maximum absolute count rate |
| 18–19 | `float32` | Maximum absolute normalized duty |
| 20–21 | — | Reserved, zero |
| 22 | `uint16` | Proposed configuration revision |
| 23–78 | — | Reserved, zero |

The DSP accepts this command only while disarmed. It validates finite values,
ordered position limits, positive velocity, and duty in `(0, 1]`, then stores
the block in the named staging revision. The active
configuration does not change until `COMMIT_CONFIGURATION` succeeds.

### 4.3 `SET_CONTROLLER_COEFFICIENTS`

| Payload word(s) | Type | Field |
|---:|---|---|
| 8 | `uint16` | Axis index 0–5 |
| 9 | `uint16` | Controller: 1 = position, 2 = velocity |
| 10 | `uint16` | Proposed configuration revision |
| 11–44 | 17 `float32` | Denominator `A[0]` through `A[16]` |
| 45–78 | 17 `float32` | Numerator `B[0]` through `B[16]` |

`NA = NB = 16`; each fixed array contains 17 coefficients. A shorter controller
is zero-padded by the host. Every coefficient must be finite, and `A[0]` must
equal `1.0`. The coefficients are discrete-time coefficients for the fixed
5 kHz controller sample rate. Commands are accepted only while disarmed and
stored in the named staging revision; they do not become active before commit.
A successful commit resets the affected controller state.

### 4.4 `SET_AUX_ENCODER_CONFIG`

| Payload word(s) | Type | Field |
|---:|---|---|
| 8 | `uint16` | Auxiliary sensor index 0–3 |
| 9 | `uint16` | Enabled: exactly 0 or 1 |
| 10 | `int16` | Count direction, exactly `-1` or `+1` |
| 11 | `uint16` | Decoder multiplier, exactly 2 for the CPU2 A-edge decoder |
| 12–13 | `uint32` | Maximum interrupt-event rate in events/second |
| 14 | `uint16` | Proposed configuration revision |
| 15–78 | — | Reserved, zero |

The DSP accepts this command only while disarmed. An enabled sensor requires a
positive event-rate limit no greater than the measured per-sensor and aggregate
CPU2 budget. Encoder cycles/revolution and public-unit conversion remain on the
Pi; the DSP stores only native counts, direction, enable, and the raw rate guard.

### 4.5 Remaining commands

`SET_HOMING_CONFIG` uses:

| Payload word(s) | Type | Field |
|---:|---|---|
| 8 | `uint16` | Axis index 0–5 |
| 9 | `int16` | Seek direction, exactly `-1` or `+1` |
| 10–11 | `float32` | Positive seek speed in counts/second |
| 12–13 | `float32` | Positive slow-reapproach speed in counts/second |
| 14–15 | `uint32` | Backoff distance in counts |
| 16–17 | `uint32` | Maximum total travel in counts |
| 18–19 | `uint32` | Timeout in milliseconds |
| 20 | `uint16` | Proposed configuration revision |
| 21–78 | — | Reserved, zero |

It is accepted only while disarmed. Values must be finite/positive where
applicable and bounded by the axis's raw velocity/travel limits.

| Type | Payload |
|---|---|
| `RESET_CONTROLLER_STATE` | Word 8 axis mask; remaining words zero |
| `ARM` | Word 8 desired axis-enable mask; word 9 expected active configuration revision |
| `DISARM` | No payload |
| `CLEAR_FAULT` | Word 8 axis mask, or zero for all clearable faults |
| `ZERO_ENCODER` | Word 8 mask: bits 0–5 motor axes and bits 8–11 auxiliary sensors; valid only while disarmed |
| `IDENTIFY` | No payload |
| `COMMIT_CONFIGURATION` | Word 8 proposed revision; word 9 axis-present mask; word 10 auxiliary-enabled mask; remaining words zero |
| `START_HOMING` | Word 8 axis index 0–5; remaining words zero |
| `ABORT_HOMING` | No payload |
| `SOFTWARE_ESTOP` | No payload; accepted from any state after target-ID/CRC validation and latches `ESTOP` |

The raw-limit block, homing block, and both complete controller blocks (position A/B and
velocity A/B) for every bit in the axis-present mask must already exist in the same
staging revision. An auxiliary config block must exist for every bit in the
auxiliary-enabled mask. Commit validates the whole set and swaps it at one 5 kHz
control boundary while disarmed; an incomplete set is rejected and leaves the
previous active revision untouched. Axes and sensors outside their masks are
disabled. Arm is rejected if configuration is invalid, the expected active
revision does not match, or a software/decoder/control fault is latched.

Homing is accepted only while disarmed and runs one axis at a time. If its
active-low switch is already active, the axis first moves away. It then seeks,
backs off until release, reapproaches slowly, latches the raw zero/offset, and
returns to disarmed. Timeout, maximum travel, watchdog, abort, or software
E-stop forces PWM zero, all STBY low, and shared EN low. Homing completion never
arms the normal controller.

## 5. DSP-to-Pi telemetry frame

| Word(s) | Type | Field |
|---:|---|---|
| 0–3 | — | Pipeline padding, transmitted as zero |
| 4 | `uint16` | Header `0xAA55` |
| 5 | `uint16` | Protocol version `4` |
| 6–7 | `uint32` | Device ID |
| 8–9 | `uint32` | DSP timestamp in microseconds, modulo `2^32` |
| 10–11 | `uint32` | Telemetry sequence |
| 12–13 | `uint32` | Last processed command sequence / acknowledgement sequence |
| 14 | `uint16` | Result code for that command |
| 15 | `uint16` | Safety state |
| 16 | `uint16` | Six packed 2-bit active modes |
| 17 | `uint16` | Active axis-enable mask |
| 18–19 | `uint32` | Fault bitmap |
| 20 | `uint16` | Active configuration revision |
| 21–32 | six `int32` | Native positions, axes 0–5 |
| 33–44 | six `float32` | Native count rates, axes 0–5 |
| 45–50 | six `int16` | Applied duty in signed Q1.15, axes 0–5 |
| 51–56 | six `uint16` | Reserved future current ADC samples; zero while disabled |
| 57–60 | four `uint16` | Reserved auxiliary ADC samples; zero while disabled |
| 61–68 | four `int32` | Native auxiliary encoder positions, sensors 0–3 |
| 69–76 | four `float32` | Native auxiliary encoder count rates, sensors 0–3 |
| 77 | `uint16` | Invalid-frame/CRC counter, saturating |
| 78 | `uint16` | Control/SPI/CPU2 deadline-or-overrun counter, saturating |
| 79 | `uint16` | CRC over words 4–78 |

Signed Q1.15 maps `-1.0` to `-32768` and clamps `+1.0` to `32767`. The host
extends the 32-bit microsecond timestamp across wrap using the telemetry sequence.

Fault bitmap:

| Bits | Meaning |
|---|---|
| 0 | Software E-stop latched |
| 1 | Homing timeout or maximum-travel fault |
| 2–11 | Reserved, zero |
| 12 | Valid-command watchdog timeout |
| 13 | Invalid or mismatched configuration |
| 14 | CLA control fault/deadline |
| 15 | CPU2 encoder-decoder overrun or handoff fault |
| 16 | Encoder signal/rate fault |
| 17 | Software travel/velocity limit |
| 18 | Homing/switch-state fault |
| 19 | SPI/DMA framing or deadline fault |
| 20–25 | Active `HOME0_N`–`HOME5_N` states after polarity normalization |
| 26–31 | Reserved, zero |

## 6. DSP DMA implementation

| Direction | DMA | Trigger | Source | Destination |
|---|---|---|---|---|
| Pi → DSP | Channel 1 | SPIA RX, `DMA_TRIGGER_SPIARX = 110` | SPI RX buffer, fixed | 80-word RAM buffer, incrementing |
| DSP → Pi | Channel 2 | SPIA TX, `DMA_TRIGGER_SPIATX = 109` | 80-word RAM buffer, incrementing | SPI TX buffer, fixed |

Use four 80-word buffers: RX active/standby and TX active/standby (640 bytes
total). DMA is one-shot for exactly 80 words.

- Before CS falls: both DMA channels are armed and the TX active buffer is ready.
- CS falling: begins one transaction; no parsing or control work occurs per word.
- CS rising: stop/inspect DMA counts, reject incomplete or oversized frames, swap
  the complete RX buffer, publish a prepared TX standby buffer if available,
  and re-arm both channels.
- The 5 kHz control loop consumes only validated, completed standby frames. At
  the 1 kHz baseline, a new frame is normally available every fifth control tick.

The CS mirror is GPIO40/Input-XBAR14/XINT5 on both edges. Remove the current
GPIO123/XINT1 frame-boundary implementation.

## 7. Validation and recovery

Reject the whole command without partial application when any of these fail:

- frame length or CS boundary;
- header or protocol version;
- target device ID;
- CRC;
- strictly newer sequence check;
- command type, payload, state, range, or configuration revision.

On rejected frames, retain the previous valid cyclic command until the 50 ms
watchdog expires, but never retain an explicit arm request. On timeout or a
safety fault, command zero duty, drive all STBY signals low and shared EN low,
and enter `DISARMED`, `FAULT`, or `ESTOP` as appropriate.

Protocol freeze requires one shared set of golden byte vectors that passes on
both the C++ host codec and C DSP codec, including:

- every command type and telemetry decode;
- positive/negative `int32`, finite float, `+0`, and `-0` values;
- byte/word order and known CRC vectors;
- bad CRC, wrong ID/version, stale sequence, and incomplete frames;
- command acknowledgement and configuration-revision behavior;
- 10,000 consecutive 1 kHz loopback transactions with no buffer corruption;
- a separate one-DSP 5 kHz trial with 50,000 consecutive transactions, no DMA
  re-arm or control deadline misses, and recorded transaction/jitter timing at
  10 MHz; repeat at 20 MHz only after signal-integrity validation if more margin
  is required;
- logic-analyzer confirmation of mode, CS timing, and all 80 words.
