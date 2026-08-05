# SPI DMA Protocol v4

Status: proposed wire contract; freeze after host/DSP golden-vector and logic-analyzer tests.

Last updated: 2026-08-04

This is the authoritative cyclic protocol between a Raspberry Pi 5 master and a
TMS320F28379D SPIA slave. It replaces the incompatible v2/v3 frame definitions
currently present in the DSP firmware.

## 1. Transport contract

| Property | Value |
|---|---|
| Master / slave | Raspberry Pi master; DSP SPIA slave |
| Transaction rate | 1,000 transactions/s, initiated by the Pi |
| Clock | 10 MHz target |
| SPI mode | Mode 3 (`CPOL=1`, `CPHA=1`), matching C2000 `SPI_PROT_POL1PHA1`; verify on both ends before freeze |
| Chip select | Active-low CE0; also mirrored to DSP GPIO40 for frame boundaries |
| Frame size | Exactly 64 × 16-bit words (128 bytes) in each direction |
| Bit order | Most-significant bit first within every 16-bit word |
| 32-bit word order | Low 16-bit word first, then high 16-bit word |
| Float format | IEEE-754 binary32 bit pattern, split low word then high word |
| Unused fields | Transmit as zero; receiver ignores only fields marked reserved |
| CRC | CRC-16/CCITT-FALSE over specified 16-bit words |

At 10 MHz, one 1,024-bit transaction takes 102.4 µs, or 10.24% of a 1 ms
period. The first four DSP-to-Pi words are pipeline padding until an on-hardware
test proves they can be removed; v4 keeps them so both frame sizes remain fixed.

CRC parameters:

- Polynomial `0x1021`
- Initial value `0xFFFF`
- `RefIn=false`, `RefOut=false`
- Final XOR `0x0000`
- Feed each 16-bit word's high byte, then low byte

## 2. Timing and ownership

```text
Pi, every 1 ms       CS low ── clocks 64 words ── CS high
DSP SPI/DMA          already armed              validate/swap/re-arm
DSP control          apply valid command at next 1 kHz control boundary
DSP telemetry        coherent snapshot from the previous completed control tick
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
| `0x0003` | `SET_CONTROLLER_PARAMS` | Configure one position or velocity PID |
| `0x0004` | `RESET_CONTROLLER_STATE` | Clear integrator/filter history |
| `0x0005` | `ARM` | Request transition from disarmed to armed |
| `0x0006` | `DISARM` | Request the safe disarmed state |
| `0x0007` | `CLEAR_FAULT` | Clear latched software faults when safe |
| `0x0008` | `ZERO_ENCODER` | Set selected native encoder counts to zero while disarmed |
| `0x0009` | `IDENTIFY` | Request an immediate status response for discovery/testing |
| `0x000A` | `COMMIT_CONFIGURATION` | Atomically activate a complete staged configuration revision |

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

## 4. Pi-to-DSP frame

| Word(s) | Type | Field |
|---:|---|---|
| 0 | `uint16` | Header `0x55AA` |
| 1 | `uint16` | Protocol version `4` |
| 2–3 | `uint32` | Target device ID |
| 4–5 | `uint32` | Command sequence |
| 6 | `uint16` | Command type |
| 7 | `uint16` | Common flags; bit 0 = acknowledgement requested, others zero |
| 8–62 | 55 words | Type-specific payload; all unused words zero |
| 63 | `uint16` | CRC over words 0–62 |

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
| 24–62 | Reserved, zero |

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
| 10 | `uint16` | Flags; bit 0 = current limit valid |
| 11 | — | Reserved, zero |
| 12–13 | `int32` | Minimum native position count |
| 14–15 | `int32` | Maximum native position count |
| 16–17 | `float32` | Maximum absolute count rate |
| 18–19 | `float32` | Maximum absolute normalized duty |
| 20–21 | `float32` | Calibrated current limit; ignored unless flag bit 0 is set |
| 22 | `uint16` | Proposed configuration revision |
| 23–62 | — | Reserved, zero |

The DSP accepts this command only while disarmed. It validates finite values,
ordered position limits, positive velocity, duty in `(0, 1]`, and any enabled
current limit, then stores the block in the named staging revision. The active
configuration does not change until `COMMIT_CONFIGURATION` succeeds.

### 4.3 `SET_CONTROLLER_PARAMS`

| Payload word(s) | Type | Field |
|---:|---|---|
| 8 | `uint16` | Axis index 0–5 |
| 9 | `uint16` | Controller: 1 = position, 2 = velocity |
| 10 | `uint16` | Flags; bit 0 = reset state on apply |
| 11 | — | Reserved, zero |
| 12–13 | `float32` | `kp` |
| 14–15 | `float32` | `ki` |
| 16–17 | `float32` | `kd` |
| 18–19 | `float32` | Absolute controller-output limit `(0, 1]` |
| 20–21 | `float32` | Velocity measurement filter cutoff in Hz; zero disables filtering |
| 22 | `uint16` | Proposed configuration revision |
| 23–62 | — | Reserved, zero |

All parameters must be finite. Applying gains while armed is rejected; this
keeps configuration changes out of a live control transition. A valid block is
stored in the named staging revision and is not active before commit.

### 4.4 Remaining commands

| Type | Payload |
|---|---|
| `RESET_CONTROLLER_STATE` | Word 8 axis mask; remaining words zero |
| `ARM` | Word 8 desired axis-enable mask; word 9 expected active configuration revision |
| `DISARM` | No payload |
| `CLEAR_FAULT` | Word 8 axis mask, or zero for all clearable faults |
| `ZERO_ENCODER` | Word 8 axis mask; valid only while disarmed |
| `IDENTIFY` | No payload |
| `COMMIT_CONFIGURATION` | Word 8 proposed revision; word 9 axis-present mask; remaining words zero |

All limit, position-PID, and velocity-PID blocks for every bit in the axis-present
mask must already exist in the same staging revision. Commit validates the whole
set and swaps it at one control boundary while disarmed; an incomplete set is
rejected and leaves the previous active revision untouched. Axes outside the
mask are disabled. Arm is rejected if configuration is invalid, the expected
active revision does not match, a driver fault is active, or the independent
enable/stop input is unsafe.

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
| 51–56 | six `uint16` | Raw current ADC samples, axes 0–5 |
| 57–60 | four `uint16` | Raw auxiliary ADC samples |
| 61 | `uint16` | Invalid-frame/CRC counter, saturating |
| 62 | `uint16` | Control/SPI deadline-miss counter, saturating |
| 63 | `uint16` | CRC over words 4–62 |

Signed Q1.15 maps `-1.0` to `-32768` and clamps `+1.0` to `32767`. The host
extends the 32-bit microsecond timestamp across wrap using the telemetry sequence.

Fault bitmap:

| Bits | Meaning |
|---|---|
| 0–5 | MC33926 driver faults for axes 0–5 |
| 6–11 | Calibrated over-current faults for axes 0–5 |
| 12 | Valid-command watchdog timeout |
| 13 | Invalid or mismatched configuration |
| 14 | CLA control fault/deadline |
| 15 | CLB encoder-decoder fault |
| 16 | Encoder signal/rate fault |
| 17 | Software travel/velocity limit |
| 18 | Independent stop/enable input unsafe |
| 19 | SPI/DMA framing or deadline fault |
| 20–27 | Active `LIMIT0_N`–`LIMIT7_N` states after polarity normalization |
| 28–31 | Reserved, zero |

## 6. DSP DMA implementation

| Direction | DMA | Trigger | Source | Destination |
|---|---|---|---|---|
| Pi → DSP | Channel 1 | SPIA RX, `DMA_TRIGGER_SPIARX = 110` | SPI RX buffer, fixed | 64-word RAM buffer, incrementing |
| DSP → Pi | Channel 2 | SPIA TX, `DMA_TRIGGER_SPIATX = 109` | 64-word RAM buffer, incrementing | SPI TX buffer, fixed |

Use four 64-word buffers: RX active/standby and TX active/standby (512 bytes
total). DMA is one-shot for exactly 64 words.

- Before CS falls: both DMA channels are armed and the TX active buffer is ready.
- CS falling: begins one transaction; no parsing or control work occurs per word.
- CS rising: stop/inspect DMA counts, reject incomplete or oversized frames, swap
  the complete RX buffer, publish a prepared TX standby buffer if available,
  and re-arm both channels.
- The 1 kHz control loop validates and consumes only completed standby frames.

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
safety fault, command zero duty, assert all disables, and enter `DISARMED` or
`FAULT` as appropriate.

Protocol freeze requires one shared set of golden byte vectors that passes on
both the C++ host codec and C DSP codec, including:

- every command type and telemetry decode;
- positive/negative `int32`, finite float, `+0`, and `-0` values;
- byte/word order and known CRC vectors;
- bad CRC, wrong ID/version, stale sequence, and incomplete frames;
- command acknowledgement and configuration-revision behavior;
- 10,000 consecutive 1 kHz loopback transactions with no buffer corruption;
- logic-analyzer confirmation of mode, CS timing, and all 64 words.
