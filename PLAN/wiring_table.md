# TMDSHSECDOCK Point-to-Point Wiring Table

Status: current prototype wiring baseline

Last updated: 2026-08-05

This is the bench hookup table for the six-axis prototype. The `Dock pin`
column contains only the numeric HSEC connector pin, as printed on the dock.
Motor channels are numbered 1–6 externally and axes 0–5 in software.

Do not connect a 5 V MC3486 output directly to a DSP GPIO. Route every encoder
logic signal through the tested 5 V-to-3.3 V translator. HSEC pin 122 is GPIO36,
not a 3.3 V supply.

## 1. Complete dock hookup table

| Group | Signal | Dock pin | DSP GPIO / peripheral | Direction | Connect to | Current status |
|---|---|---:|---|---|---|---|
| Motor 1 / axis 0 | `PWM1` | 49 | GPIO0 / ePWM1A | Output | H-bridge PCB `PWM1` | Wire now |
| Motor 1 / axis 0 | `STBY1` | 51 | GPIO1 | Output | H-bridge PCB `STBY1` → MC33926 `D2` | Wire now; high permits drive |
| Motor 1 / axis 0 | `DIR1` | 58 | GPIO12 | Output | H-bridge PCB `DIR1` | Wire now |
| Motor 1 / axis 0 | `CURRENT1` | 9 | ADCA0 | Analog input | Future conditioned current feedback | Reserved; leave open |
| Motor 2 / axis 1 | `PWM2` | 53 | GPIO2 / ePWM2A | Output | H-bridge PCB `PWM2` | Wire now |
| Motor 2 / axis 1 | `STBY2` | 55 | GPIO3 | Output | H-bridge PCB `STBY2` → MC33926 `D2` | Wire now; high permits drive |
| Motor 2 / axis 1 | `DIR2` | 60 | GPIO13 | Output | H-bridge PCB `DIR2` | Wire now |
| Motor 2 / axis 1 | `CURRENT2` | 11 | ADCA1 | Analog input | Future conditioned current feedback | Reserved; leave open |
| Motor 3 / axis 2 | `PWM3` | 50 | GPIO4 / ePWM3A | Output | H-bridge PCB `PWM3` | Wire now |
| Motor 3 / axis 2 | `STBY3` | 52 | GPIO5 | Output | H-bridge PCB `STBY3` → MC33926 `D2` | Wire now; high permits drive |
| Motor 3 / axis 2 | `DIR3` | 62 | GPIO14 | Output | H-bridge PCB `DIR3` | Wire now |
| Motor 3 / axis 2 | `CURRENT3` | 15 | ADCA2 | Analog input | Future conditioned current feedback | Reserved; leave open |
| Motor 4 / axis 3 | `PWM4` | 54 | GPIO6 / ePWM4A | Output | H-bridge PCB `PWM4` | Wire now |
| Motor 4 / axis 3 | `STBY4` | 56 | GPIO7 | Output | H-bridge PCB `STBY4` → MC33926 `D2` | Wire now; high permits drive |
| Motor 4 / axis 3 | `DIR4` | 64 | GPIO15 | Output | H-bridge PCB `DIR4` | Wire now |
| Motor 4 / axis 3 | `CURRENT4` | 17 | ADCA3 | Analog input | Future conditioned current feedback | Reserved; leave open |
| Motor 5 / axis 4 | `PWM5` | 57 | GPIO8 / ePWM5A | Output | H-bridge PCB `PWM5` | Wire now |
| Motor 5 / axis 4 | `STBY5` | 59 | GPIO9 | Output | H-bridge PCB `STBY5` → MC33926 `D2` | Wire now; high permits drive |
| Motor 5 / axis 4 | `DIR5` | 67 | GPIO16 | Output | H-bridge PCB `DIR5` | Wire now |
| Motor 5 / axis 4 | `CURRENT5` | 21 | ADCA4 | Analog input | Future conditioned current feedback | Reserved; leave open |
| Motor 6 / axis 5 | `PWM6` | 61 | GPIO10 / ePWM6A | Output | H-bridge PCB `PWM6` | Wire now |
| Motor 6 / axis 5 | `STBY6` | 63 | GPIO11 | Output | H-bridge PCB `STBY6` → MC33926 `D2` | Wire now; high permits drive |
| Motor 6 / axis 5 | `DIR6` | 69 | GPIO17 | Output | H-bridge PCB `DIR6` | Wire now |
| Motor 6 / axis 5 | `CURRENT6` | 23 | ADCA5 | Analog input | Future conditioned current feedback | Reserved; leave open |
| All motors | `MOTOR_EN` | 71 | GPIO18 | Output | Branch to H-bridge PCB `EN1`–`EN6` | Wire now; low disables all |
| Encoder 1 / axis 0 | `ENC1_A` | 68 | GPIO20 / eQEP1A | Input | Translator A-side output for encoder 1 A | Already locked/wired |
| Encoder 1 / axis 0 | `ENC1_B` | 70 | GPIO21 / eQEP1B | Input | Translator A-side output for encoder 1 B | Already locked/wired |
| Encoder 1 / axis 0 | `ENC1_I` | 74 | GPIO23 / eQEP1I | Input | Translator A-side output for encoder 1 index | Already locked/wired |
| Encoder 2 / axis 1 | `ENC2_A` | 100 | GPIO54 / eQEP2A | Input | Translator A-side output for encoder 2 A | Already locked/wired |
| Encoder 2 / axis 1 | `ENC2_B` | 102 | GPIO55 / eQEP2B | Input | Translator A-side output for encoder 2 B | Already locked/wired |
| Encoder 2 / axis 1 | `ENC2_I` | 106 | GPIO57 / eQEP2I | Input | Translator A-side output for encoder 2 index | Already locked/wired |
| Encoder 3 / axis 2 | `ENC3_A` | 127 | GPIO62 / eQEP3A | Input | Translator A-side output for encoder 3 A | Already locked/wired |
| Encoder 3 / axis 2 | `ENC3_B` | 128 | GPIO63 / eQEP3B | Input | Translator A-side output for encoder 3 B | Already locked/wired |
| Encoder 3 / axis 2 | `ENC3_I` | 130 | GPIO65 / eQEP3I | Input | Translator A-side output for encoder 3 index | Already locked/wired |
| Encoder 4 / axis 3 | `ENC4_A` | 75 | GPIO24 / Input-XBAR7 → eCAP1 (CPU2) | Input | Translator A-side output for encoder 4 A | Wire now |
| Encoder 4 / axis 3 | `ENC4_B` | 77 | GPIO25 / Input-XBAR8 → eCAP2 (CPU2) | Input | Translator A-side output for encoder 4 B | Wire now |
| Encoder 5 / axis 4 | `ENC5_A` | 79 | GPIO26 / Input-XBAR9 → eCAP3 (CPU2) | Input | Translator A-side output for encoder 5 A | Wire now |
| Encoder 5 / axis 4 | `ENC5_B` | 81 | GPIO27 / Input-XBAR10 → eCAP4 (CPU2) | Input | Translator A-side output for encoder 5 B | Wire now |
| Encoder 6 / axis 5 | `ENC6_A` | 80 | GPIO30 / Input-XBAR11 → eCAP5 (CPU2) | Input | Translator A-side output for encoder 6 A | Wire now |
| Encoder 6 / axis 5 | `ENC6_B` | 88 | GPIO39 / Input-XBAR12 → eCAP6 (CPU2) | Input | Translator A-side output for encoder 6 B | Wire now |
| Auxiliary encoder 0 | `AUX_ENC0_A` | 129 | GPIO64 / Input-XBAR4 → XINT1 (CPU2) | Input | Translator A-side output for auxiliary encoder 0 A | Optional |
| Auxiliary encoder 0 | `AUX_ENC0_B` | 131 | GPIO66 / direction sample (CPU2) | Input | Translator A-side output for auxiliary encoder 0 B | Optional |
| Auxiliary encoder 1 | `AUX_ENC1_A` | 132 | GPIO67 / Input-XBAR5 → XINT2 (CPU2) | Input | Translator A-side output for auxiliary encoder 1 A | Optional |
| Auxiliary encoder 1 | `AUX_ENC1_B` | 133 | GPIO68 / direction sample (CPU2) | Input | Translator A-side output for auxiliary encoder 1 B | Optional |
| Auxiliary encoder 2 | `AUX_ENC2_A` | 134 | GPIO69 / Input-XBAR6 → XINT3 (CPU2) | Input | Translator A-side output for auxiliary encoder 2 A | Optional |
| Auxiliary encoder 2 | `AUX_ENC2_B` | 137 | GPIO70 / direction sample (CPU2) | Input | Translator A-side output for auxiliary encoder 2 B | Optional |
| Auxiliary encoder 3 | `AUX_ENC3_A` | 138 | GPIO71 / Input-XBAR13 → XINT4 (CPU2) | Input | Translator A-side output for auxiliary encoder 3 A | Optional |
| Auxiliary encoder 3 | `AUX_ENC3_B` | 140 | GPIO73 / direction sample (CPU2) | Input | Translator A-side output for auxiliary encoder 3 B | Optional |
| Pi SPI | MOSI | 108 | GPIO58 / SPIA SIMO | Input | Pi physical 19 / BCM10 | Wire now |
| Pi SPI | MISO | 110 | GPIO59 / SPIA SOMI | Output | Pi physical 21 / BCM9 | Wire now |
| Pi SPI | SCLK | 125 | GPIO60 / SPIA CLK | Input | Pi physical 23 / BCM11 | Wire now |
| Pi SPI | CE0 / STE | 126 | GPIO61 / SPIA STE | Input | Pi physical 24 / BCM8 | Wire now |
| Pi SPI | CE0 mirror | 89 | GPIO40 / Input-XBAR14 → XINT5 | Input | Branch the same Pi physical 24 / BCM8 signal | Wire now |
| Home axis 0 | `HOME0_N` | 141 | GPIO74, CPU1 5 kHz polling | Input | Switch between GPIO74 and logic GND | Wire now; internal pull-up |
| Home axis 1 | `HOME1_N` | 142 | GPIO75, CPU1 5 kHz polling | Input | Switch between GPIO75 and logic GND | Wire now; internal pull-up |
| Home axis 2 | `HOME2_N` | 143 | GPIO76, CPU1 5 kHz polling | Input | Switch between GPIO76 and logic GND | Wire now; internal pull-up |
| Home axis 3 | `HOME3_N` | 144 | GPIO77, CPU1 5 kHz polling | Input | Switch between GPIO77 and logic GND | Wire now; internal pull-up |
| Home axis 4 | `HOME4_N` | 145 | GPIO78, CPU1 5 kHz polling | Input | Switch between GPIO78 and logic GND | Wire now; internal pull-up |
| Home axis 5 | `HOME5_N` | 146 | GPIO79, CPU1 5 kHz polling | Input | Switch between GPIO79 and logic GND | Wire now; internal pull-up |
| Auxiliary ADC 0 | `AUX_ADC0` | 12 | ADCB0 | Analog input | Future conditioned 0–3.3 V analog signal | Reserved; leave open/disabled |
| Auxiliary ADC 1 | `AUX_ADC1` | 14 | ADCB1 | Analog input | Future conditioned 0–3.3 V analog signal | Reserved; leave open/disabled |
| Auxiliary ADC 2 | `AUX_ADC2` | 18 | ADCB2 | Analog input | Future conditioned 0–3.3 V analog signal | Reserved; leave open/disabled |
| Auxiliary ADC 3 | `AUX_ADC3` | 20 | ADCB3 | Analog input | Future conditioned 0–3.3 V analog signal | Reserved; leave open/disabled |
| Auxiliary ADC 4 | `AUX_ADC4` | 24 | ADCB4 | Analog input | Future conditioned 0–3.3 V analog signal | Reserved; leave open/disabled |
| Auxiliary ADC 5 | `AUX_ADC5` | 26 | ADCB5 | Analog input | Future conditioned 0–3.3 V analog signal | Reserved; leave open/disabled |
| Auxiliary ADC 6 | `AUX_ADC6` | 31 | ADCC2 | Analog input | Future conditioned 0–3.3 V analog signal | Reserved; leave open/disabled |
| Auxiliary ADC 7 | `AUX_ADC7` | 33 | ADCC3 | Analog input | Future conditioned 0–3.3 V analog signal | Reserved; leave open/disabled |
| Logic ground | `GND` | 135 or 157 | GND | — | Pi ground, translator ground, encoder-PCB logic ground, and H-bridge logic ground | Wire now |

GPIO32, GPIO33, GPIO35–GPIO38, and GPIO80–GPIO82 are unassigned spares. There
is no DSP `SF` input and no DSP external-E-stop-status input in this pass. The
external E-stop remains entirely physical.

Do not route motor return current through the Pi or dock jumper ground. Join the
logic reference to motor-power ground only at the intended point in the
H-bridge/power system.

## 2. Encoder receiver and translator wiring

The encoder PCB already converts each differential pair with an MC3486. Wire
the receiver's 5 V logic outputs to the TXS0108E B side and the corresponding
DSP signals above to the A side.

| Translator connection | Connect to |
|---|---|
| `VCCA` | Stable regulated 3.3 V logic rail |
| `VCCB` | Same regulated 5 V rail used by the MC3486 output stage |
| `GND` | Common logic ground |
| `OE` | 10 kΩ pull-down to GND; enable from 3.3 V only after both rails are stable |
| `B1`–`B8` | MC3486 5 V logic outputs |
| `A1`–`A8` | Matching DSP GPIO rows from Section 1 |

Keep the translator wiring short. The tested unit produced a clean 3.3 V
signal, but this MC3486/TXS pairing is not guaranteed by worst-case input-level
specifications; scope every channel before maximum-rate operation.

The HEDL-5540 differential connector is:

| HEDL pin | Wire color | Signal | Connect to encoder PCB |
|---:|---|---|---|
| 1 | Brown | NC | Leave open |
| 2 | Red | `+5 V` | Regulated encoder 5 V |
| 3 | Orange | GND | Encoder-PCB logic ground |
| 4 | Yellow | NC | Leave open |
| 5 | Green | `A+` | Receiver A non-inverting input |
| 6 | Blue | `A−` | Receiver A inverting input |
| 7 | Violet | `B+` | Receiver B non-inverting input |
| 8 | Grey | `B−` | Receiver B inverting input |
| 9 | White | `I+` | Receiver index non-inverting input |
| 10 | Black | `I−` | Receiver index inverting input |

Axes 3–5 have no DSP index input in this pass; leave those translated index
outputs unconnected.

## 3. MC33926 fixed wiring already on the custom PCB

| MC33926 / PCB signal | Connection or rule |
|---|---|
| PCB `PWM` and `DIR` | Use the PCB inputs as labeled; their intermediate logic is accepted as the board contract |
| Native `EN` | PCB `EN`; all six PCB EN inputs branch from shared `MOTOR_EN` |
| Native `D2` | PCB `STBY`; one separate DSP GPIO per axis |
| Native `D1` | Grounded on the existing PCB |
| Native `SLEW` | Pulled to ground through 1 kΩ; DSP PWM is fixed at 10 kHz |
| `SF` | Not exposed by the current PCB; do not wire a DSP fault input |
| `FB` | Not exposed by the current PCB; ADCA0–5 remain reserved and disabled |

At reset and before software arm: PWM = 0, every `STBY` = low, and shared
`MOTOR_EN` = low. A software E-stop latches the same state. The external E-stop
is a separate physical circuit and is not wired to the DSP.

## 4. Pre-power checklist

- [ ] Dock orientation and every numeric HSEC pin are checked against the board.
- [ ] No MC3486 5 V output is connected directly to DSP GPIO.
- [ ] Every connected encoder channel is scoped at the DSP side and stays in
      the 0–3.3 V range.
- [ ] `MOTOR_EN` is low, all six `STBY` lines are low, and all PWM duties are
      zero before motor power is applied.
- [ ] Each home switch reads high when open and low when pressed.
- [ ] Pi, DSP, translator, encoder PCB, and H-bridge logic share a reference.
- [ ] Motor-current return does not flow through logic-ground jumpers.
- [ ] CE0 reaches both dock pins 126 and 89.
- [ ] The first energized motor test uses one axis, 2% duty, and a 0.5 A supply
      current limit.
