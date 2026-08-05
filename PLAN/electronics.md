# Electronics and DSP Pin Assignment

Status: provisional wiring baseline; complete the M0 checks before energized tests.

Last updated: 2026-08-04

This is the authoritative first-pass hardware interface for the Raspberry Pi 5,
TMS320F28379D controlCARD, six MC33926 motor channels, and incremental encoders.
The software remains configurable for general brushed DC motors and A/B encoders.

## 1. Electrical architecture

```text
encoder differential A/B(/I)
        │
        ▼
RS-422 receiver ── 3.3 V logic ──► DSP encoder GPIO
                                      │
Pi 5 ◄──────────── SPIA ──────────────┤
                                      │
DSP PWM/DIR/DISABLE/ENABLE ──────────► MC33926 ──► motor
DSP ADC ◄──────── CURRENT/aux analog ─┘
```

The physical E-stop must remove motor energy or bridge enable independently of
the Pi, ROS 2, and normal DSP software.

## 2. Reference axis

These values are configuration data, not firmware constants.

| Component | Part | First-pass data |
|---|---|---|
| Motor | Maxon 148867, RE 40, graphite brushes, 150 W | 24 V nominal; 6 A nominal/max-continuous; 7,580 rpm no-load; 12,000 rpm mechanical maximum |
| Gearhead | Maxon 203116, GP 42 C | absolute ratio `91/6`; 8,000 rpm maximum continuous input |
| Encoder | Maxon 110514, HEDL-5540 | 500 cycles/turn; A/B/index; 5 V DS26LS31 differential line driver |

The motor's 6 A rating exceeds the MC33926's stated 5 A limit, and the actual
continuous driver current depends on PCB cooling. Treat this combination as
derated: begin one-axis tests with a 0.5 A bench-supply limit and raise it only
after current calibration and thermal testing. Full 150 W operation is not a
first-pass claim.

Derived reference scale:

| Quantity | Value |
|---|---:|
| Quadrature edges per motor revolution | `500 × 4 = 2,000` counts |
| Counts per output revolution | `2,000 × 91/6 = 30,333.333...` counts |
| Counts per output radian | `30,333.333... / 2π = 4,827.699940` counts/rad |
| Worst-case edge rate at 12,000 motor rpm | `400,000` counts/s per axis |

The DSP uses only native counts and counts/second. The Pi converts geared-output
radians and radians/second using the configured encoder scale, ratio, sign, and
zero.

## 3. MC33926 board contract

Use these logical board signal names even if the existing PCB silkscreen differs.

| Signal | DSP type | MC33926 pin/function | Required behavior |
|---|---|---|---|
| `M<n>_PWM` | ePWMxA output | `IN1` | Duty magnitude; 0–20 kHz |
| `M<n>_DISABLE` | ePWMxB output | `D1` | Active high; trip/disarm must force high or high-impedance |
| `M<n>_DIR` | GPIO output | `INV` | Direction; change only at zero duty |
| `MOTOR_EN` | GPIO output | all `EN` | Shared high-awake/low-sleep |
| `M<n>_FAULT_N` | GPIO input | `SF` | Active-low open-drain; pull up to 3.3 V |
| `M<n>_CURRENT` | ADC input | `FB` | Calibrated and filtered current feedback |

Fixed wiring assumptions:

- `IN2` is tied low and `D2` is tied high.
- `SLEW` is tied high for 20 kHz PWM. If it is tied low, configure at most
  10 kHz.
- Each `D1` has an external pull-up; `EN` has an external pull-down.
- Each `SF` is pulled up to 3.3 V, not 5 V.
- `FB` uses `100 Ω < RFB < 300 Ω`; start with 270 Ω and verify that its filtered
  voltage cannot exceed the DSP ADC range. The bridge datasheet's approximately
  1 µF filter may be required.

Arm sequence: command zero PWM and direction, raise `MOTOR_EN`, wait for the
bridge to become operational, clear faults only on an explicit command, then
drive `M<n>_DISABLE` low. Disarm sequence: assert every disable, command zero
PWM, then lower `MOTOR_EN`. Reverse only at zero duty after at least one PWM
period.

The MC33926's digital inputs accept 3.3 V DSP logic. The remaining voltage issue
is the encoder receiver path, not the bridge control path.

## 4. Encoder electrical interface

The HEDL-5540 produces differential RS-422 signals. The present encoder PCB uses
an MC3486 receiver, whose outputs are 5 V TTL-compatible logic. TTL-compatible
does not mean 3.3 V-safe: its guaranteed high is only 2.7 V, while its actual
high may approach its 5 V supply.

F28379D GPIO is not 5 V tolerant. Treat approximately `VDDIO + 0.3 V` (about
3.6 V) as the recommended input ceiling; the larger absolute-maximum value is
not an operating target.

Temporary prototype decision:

1. Measure MC3486 `VOH` and `VOL` at the actual connector with the expected load,
   both idle and switching.
2. Direct connection is permitted only as a documented prototype exception if
   measured high/low levels satisfy the DSP `VIH`/`VIL` thresholds and the high
   never exceeds its input limit. It is not a production design because MC3486
   does not guarantee that ceiling.
3. A TXS0108E may be tried only if the MC3486 output also satisfies its 5 V-side
   input thresholds. With `VCCB = 5 V`, TXS0108E requires approximately
   `VIH >= VCCB - 0.4 V = 4.6 V` and `VIL <= 0.15 V`; MC3486 does not guarantee
   that high level. Measure waveforms at maximum encoder rate with an
   oscilloscope before use.

For that prototype, use `VCCA = 3.3 V` toward the DSP, `VCCB = 5 V` toward the
MC3486, hold `OE` low until both rails are stable, and keep wiring short. Do not
add strong pull resistors or capacitive filtering to the TXS signal pins.

Do not use a 74HC4051: it is an analog multiplexer, not a logic-level receiver
or translator.

For the next custom PCB, replace the MC3486/translator chain with 3.3 V
`AM26LV32E` quad RS-422 receivers connected directly to the DSP. Fit 100–120 Ω
termination at the receiver, hold `G` high and `/G` low, add 0.1 µF decoupling
at each IC, and provide a signal reference between boards. Six A/B channels plus
three index channels require 15 receiver channels (four ICs); index on all six
requires 18 channels (five ICs).

## 5. DSP pin assignment

`HSEC` is the 180-pin controlCARD connector number. The assignment avoids
GPIO28/29 (switch/FTDI), GPIO31/34 (LEDs), GPIO42/43/46/47 (USB), and GPIO72/84
(boot configuration).

For literal dock-to-device connections, use the consolidated
[TMDSHSECDOCK point-to-point wiring table](wiring_table.md).

### 5.1 Motor channels

| Axis | `M<n>_PWM` | `M<n>_DISABLE` | `M<n>_DIR` | `M<n>_CURRENT` |
|---:|---|---|---|---|
| 0 | GPIO0 / HSEC49 / ePWM1A | GPIO1 / HSEC51 / ePWM1B | GPIO12 / HSEC58 | ADCA0 / HSEC9 |
| 1 | GPIO2 / HSEC53 / ePWM2A | GPIO3 / HSEC55 / ePWM2B | GPIO13 / HSEC60 | ADCA1 / HSEC11 |
| 2 | GPIO4 / HSEC50 / ePWM3A | GPIO5 / HSEC52 / ePWM3B | GPIO14 / HSEC62 | ADCA2 / HSEC15 |
| 3 | GPIO6 / HSEC54 / ePWM4A | GPIO7 / HSEC56 / ePWM4B | GPIO15 / HSEC64 | ADCA3 / HSEC17 |
| 4 | GPIO8 / HSEC57 / ePWM5A | GPIO9 / HSEC59 / ePWM5B | GPIO16 / HSEC67 | ADCA4 / HSEC21 |
| 5 | GPIO10 / HSEC61 / ePWM6A | GPIO11 / HSEC63 / ePWM6B | GPIO17 / HSEC69 | ADCA5 / HSEC23 |

Shared enable: `MOTOR_EN` = GPIO18 / HSEC71.

| Axis | `M<n>_FAULT_N` |
|---:|---|
| 0 | GPIO32 / HSEC85 |
| 1 | GPIO33 / HSEC87 |
| 2 | GPIO35 / HSEC121 |
| 3 | GPIO36 / HSEC122 |
| 4 | GPIO37 / HSEC123 |
| 5 | GPIO38 / HSEC124 |

### 5.2 Primary encoders

| Axis | A | B | Index | Decoder |
|---:|---|---|---|---|
| 0 | GPIO20 / HSEC68 | GPIO21 / HSEC70 | GPIO23 / HSEC74 | eQEP1 |
| 1 | GPIO54 / HSEC100 | GPIO55 / HSEC102 | GPIO57 / HSEC106 | eQEP2 |
| 2 | GPIO62 / HSEC127 | GPIO63 / HSEC128 | GPIO65 / HSEC130 | eQEP3 |
| 3 | GPIO24 / HSEC75 | GPIO25 / HSEC77 | not assigned | CLB1 candidate |
| 4 | GPIO26 / HSEC79 | GPIO27 / HSEC81 | not assigned | CLB2 candidate |
| 5 | GPIO30 / HSEC80 | GPIO39 / HSEC88 | not assigned | CLB3 candidate |

Axes 3–5 remain conditional on a measured CLB quadrature-decoder prototype at
400 kcounts/s. If it fails, change the hardware requirement before completing
the firmware; do not silently replace it with per-edge CPU interrupts.

### 5.3 Raspberry Pi SPI

| Signal | Raspberry Pi 5 | DSP |
|---|---|---|
| MOSI | BCM10 / physical 19 | GPIO58 / HSEC108 / SPIA SIMO input |
| MISO | BCM9 / physical 21 | GPIO59 / HSEC110 / SPIA SOMI output |
| SCLK | BCM11 / physical 23 | GPIO60 / HSEC125 / SPIA CLK input |
| CE0 | BCM8 / physical 24 | GPIO61 / HSEC126 / SPIA STE input **and** GPIO40 / HSEC89 frame-boundary input |
| GND | physical 6 or another ground | common ground |

The CE0 mirror uses Input-XBAR14 and XINT5 on both edges. Existing firmware
still using GPIO123/XINT1 must be migrated before protocol validation.

### 5.4 Limit switches and reserved slow encoders

The slow encoders are reserved but deferred. The limit bank is sampled in the
first pass without per-edge interrupts.

| Function | A/input | B |
|---|---|---|
| Slow encoder 0 | GPIO64 / HSEC129 | GPIO66 / HSEC131 |
| Slow encoder 1 | GPIO67 / HSEC132 | GPIO68 / HSEC133 |
| Slow encoder 2 | GPIO69 / HSEC134 | GPIO70 / HSEC137 |
| Slow encoder 3 | GPIO71 / HSEC138 | GPIO73 / HSEC140 |
| `LIMIT0_N`–`LIMIT7_N` | GPIO74–GPIO81 / HSEC141–HSEC148 | — |

Use 3.3 V pull-ups and active-low switch inputs. Read all eight as one GPIO bank
snapshot in the 1 kHz loop; they do not consume eight CPU interrupts. Their
axis/direction mapping and whether an active switch causes per-axis inhibit or a
global disarm must be fixed in the machine profile before use. An active input
always inhibits arming until that policy is defined. GPIO65 remains eQEP3 index
and GPIO72 remains a boot pin.

External stop status: `ESTOP_OK` = GPIO82 / HSEC149, 3.3 V input, sampled at
1 kHz. This is status feedback only. The physical E-stop circuit must directly
gate the bridge enable/energy path without depending on this GPIO or software.

### 5.5 Auxiliary analog inputs

| Channel | Pin | First-pass use |
|---:|---|---|
| 0 | ADCB0 / HSEC12 | cyclic telemetry candidate |
| 1 | ADCB1 / HSEC14 | cyclic telemetry candidate |
| 2 | ADCB2 / HSEC18 | cyclic telemetry candidate |
| 3 | ADCB3 / HSEC20 | cyclic telemetry candidate |
| 4 | ADCB4 / HSEC24 | reserved |
| 5 | ADCB5 / HSEC26 | reserved |
| 6 | ADCC2 / HSEC31 | reserved |
| 7 | ADCC3 / HSEC33 | reserved |

The SPI telemetry frame carries four auxiliary samples. Freeze which four after
the analog continuity check.

## 6. Resource budget

| Resource | Required | Result |
|---|---:|---|
| Digital GPIO | 62 including E-stop status, limits, and reserved slow encoders | Fits provisional map |
| ADC channels | 6 current + 8 auxiliary | Fits 14 HSEC analog inputs |
| ePWM modules | ePWM1–6 A/B | Fits |
| eQEP modules | 3 | All used |
| CLB tiles | 3 candidates for axes 3–5 | One tile remains; performance must be proven |
| Input X-BAR | 1–6 for CLB encoder edges; 14 for SPI boundary | Fits, but no six independent fault trips remain |
| CPU external interrupts | XINT5 for SPI; XINT4 free | Limits use polling, not eight interrupts |

The 62 digital signals are 25 motor-control/fault signals, 15 primary-encoder
signals, 5 SPI/frame-boundary signals, 8 reserved slow-encoder signals, 8 limit
inputs, and 1 E-stop status input.

The provisional encoder allocation cannot also route all six `SF` signals to
independent ePWM trip inputs. The baseline therefore uses MC33926 internal
protection, 1 kHz DSP fault observation, and the independent external stop/enable
path. If asynchronous per-axis `SF` shutdown is mandatory, add external fault
aggregation/counter hardware or reduce encoder requirements.

## 7. M0 hardware gates

- [ ] Trace every custom-PCB signal to the MC33926 or encoder connector.
- [ ] Confirm `D1`, `EN`, `SF`, `SLEW`, `IN2`, and `D2` passive states.
- [ ] Measure MC3486 output levels and maximum-rate A/B waveforms; select and
      document the temporary receiver/translation path.
- [ ] Verify every selected HSEC pin against the exact controlCARD revision and
      C2000 pin-mux tables.
- [ ] Prove one CLB quadrature decoder at 400 kcounts/s with direction changes.
- [ ] Define and bench-test the independent E-stop/enable circuit.
- [ ] Define each limit input's axis, direction, polarity, and stop/recovery action.
- [ ] Verify all current/aux ADC voltages remain within the DSP ADC range.
- [ ] Start energized testing on one axis at 2% duty and a 0.5 A supply limit.

No six-axis power test starts until these gates pass.

## 8. Reference documents

- [F28379D datasheet](../doc/F28379D_Datasheet.pdf)
- [F28379D technical reference manual](../doc/F28379D_User_Guide.pdf)
- [F28379D controlCARD guide](<../doc/TMS320F28379D controlCARD.pdf>) and
  [revision 1.3 schematic](../doc/F2837x_180controlCARD_R1_3_SCH_02Oct2015.pdf)
- [MC33926 datasheet](../doc/MC33926.pdf)
- [MC3486 datasheet](../doc/mc3486.pdf)
- [TXS0108E datasheet](../doc/txs0108e.pdf)
- [HEDL-5540 family datasheet](<../doc/HEDL-5zzz, 9zzz Series.pdf>)
- [AM26LV32E datasheet](https://www.ti.com/lit/ds/symlink/am26lv32e.pdf)
- [Maxon 148867 motor data](https://www.maxongroup.com/medias/sys_master/root/9398013394974/Cataloge-Page-EN-163.pdf),
  [Maxon 203116 gearhead data](https://www.maxongroup.com/medias/sys_master/root/8831071158302/2018EN-354.pdf),
  and [Maxon 110514 encoder data](https://www.maxongroup.com/maxon/view/product/110514)
