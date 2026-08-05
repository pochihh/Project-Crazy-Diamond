# TMDSHSECDOCK Point-to-Point Wiring Table

Status: provisional hookup checklist; verify continuity before applying power.

Last updated: 2026-08-04

This table converts the project pin assignment into literal bench connections.
`HSEC` means the numbered pin on the 180-pin F28379D controlCARD/dock connector.
Motor channels are numbered 1–6 externally and axes 0–5 in software.

The custom encoder/H-bridge PCB connector numbering is unknown. Its entries use
logical labels (`PWM1`, `ENC1_A`, and so on); identify the real connector pins
with a continuity meter and write them in the final column before wiring.

## 1. Complete dock hookup table

| Group | Signal | DSP dock endpoint | Direction at DSP | Connect to | Custom-PCB connector pin |
|---|---|---|---|---|---|
| Motor 1 / axis 0 | `PWM1` | HSEC49 — GPIO0 / ePWM1A | Output | H-bridge `PWM1` → MC33926 `IN1` | TBD by continuity |
| Motor 1 / axis 0 | `DISABLE1` | HSEC51 — GPIO1 / ePWM1B | Output | H-bridge `DISABLE1` → MC33926 `D1` | TBD by continuity |
| Motor 1 / axis 0 | `DIR1` | HSEC58 — GPIO12 | Output | H-bridge `DIR1` → MC33926 `INV` | TBD by continuity |
| Motor 1 / axis 0 | `FAULT1_N` | HSEC85 — GPIO32 | Input | H-bridge `FAULT1_N` ← MC33926 `SF`, pulled up to 3.3 V | TBD by continuity |
| Motor 1 / axis 0 | `CURRENT1` | HSEC9 — ADCA0 | Analog input | H-bridge `CURRENT1` ← filtered MC33926 `FB`, 0–3.3 V only | TBD by continuity |
| Motor 2 / axis 1 | `PWM2` | HSEC53 — GPIO2 / ePWM2A | Output | H-bridge `PWM2` → MC33926 `IN1` | TBD by continuity |
| Motor 2 / axis 1 | `DISABLE2` | HSEC55 — GPIO3 / ePWM2B | Output | H-bridge `DISABLE2` → MC33926 `D1` | TBD by continuity |
| Motor 2 / axis 1 | `DIR2` | HSEC60 — GPIO13 | Output | H-bridge `DIR2` → MC33926 `INV` | TBD by continuity |
| Motor 2 / axis 1 | `FAULT2_N` | HSEC87 — GPIO33 | Input | H-bridge `FAULT2_N` ← MC33926 `SF`, pulled up to 3.3 V | TBD by continuity |
| Motor 2 / axis 1 | `CURRENT2` | HSEC11 — ADCA1 | Analog input | H-bridge `CURRENT2` ← filtered MC33926 `FB`, 0–3.3 V only | TBD by continuity |
| Motor 3 / axis 2 | `PWM3` | HSEC50 — GPIO4 / ePWM3A | Output | H-bridge `PWM3` → MC33926 `IN1` | TBD by continuity |
| Motor 3 / axis 2 | `DISABLE3` | HSEC52 — GPIO5 / ePWM3B | Output | H-bridge `DISABLE3` → MC33926 `D1` | TBD by continuity |
| Motor 3 / axis 2 | `DIR3` | HSEC62 — GPIO14 | Output | H-bridge `DIR3` → MC33926 `INV` | TBD by continuity |
| Motor 3 / axis 2 | `FAULT3_N` | HSEC121 — GPIO35 | Input | H-bridge `FAULT3_N` ← MC33926 `SF`, pulled up to 3.3 V | TBD by continuity |
| Motor 3 / axis 2 | `CURRENT3` | HSEC15 — ADCA2 | Analog input | H-bridge `CURRENT3` ← filtered MC33926 `FB`, 0–3.3 V only | TBD by continuity |
| Motor 4 / axis 3 | `PWM4` | HSEC54 — GPIO6 / ePWM4A | Output | H-bridge `PWM4` → MC33926 `IN1` | TBD by continuity |
| Motor 4 / axis 3 | `DISABLE4` | HSEC56 — GPIO7 / ePWM4B | Output | H-bridge `DISABLE4` → MC33926 `D1` | TBD by continuity |
| Motor 4 / axis 3 | `DIR4` | HSEC64 — GPIO15 | Output | H-bridge `DIR4` → MC33926 `INV` | TBD by continuity |
| Motor 4 / axis 3 | `FAULT4_N` | HSEC122 — GPIO36 | Input | H-bridge `FAULT4_N` ← MC33926 `SF`, pulled up to 3.3 V | TBD by continuity |
| Motor 4 / axis 3 | `CURRENT4` | HSEC17 — ADCA3 | Analog input | H-bridge `CURRENT4` ← filtered MC33926 `FB`, 0–3.3 V only | TBD by continuity |
| Motor 5 / axis 4 | `PWM5` | HSEC57 — GPIO8 / ePWM5A | Output | H-bridge `PWM5` → MC33926 `IN1` | TBD by continuity |
| Motor 5 / axis 4 | `DISABLE5` | HSEC59 — GPIO9 / ePWM5B | Output | H-bridge `DISABLE5` → MC33926 `D1` | TBD by continuity |
| Motor 5 / axis 4 | `DIR5` | HSEC67 — GPIO16 | Output | H-bridge `DIR5` → MC33926 `INV` | TBD by continuity |
| Motor 5 / axis 4 | `FAULT5_N` | HSEC123 — GPIO37 | Input | H-bridge `FAULT5_N` ← MC33926 `SF`, pulled up to 3.3 V | TBD by continuity |
| Motor 5 / axis 4 | `CURRENT5` | HSEC21 — ADCA4 | Analog input | H-bridge `CURRENT5` ← filtered MC33926 `FB`, 0–3.3 V only | TBD by continuity |
| Motor 6 / axis 5 | `PWM6` | HSEC61 — GPIO10 / ePWM6A | Output | H-bridge `PWM6` → MC33926 `IN1` | TBD by continuity |
| Motor 6 / axis 5 | `DISABLE6` | HSEC63 — GPIO11 / ePWM6B | Output | H-bridge `DISABLE6` → MC33926 `D1` | TBD by continuity |
| Motor 6 / axis 5 | `DIR6` | HSEC69 — GPIO17 | Output | H-bridge `DIR6` → MC33926 `INV` | TBD by continuity |
| Motor 6 / axis 5 | `FAULT6_N` | HSEC124 — GPIO38 | Input | H-bridge `FAULT6_N` ← MC33926 `SF`, pulled up to 3.3 V | TBD by continuity |
| Motor 6 / axis 5 | `CURRENT6` | HSEC23 — ADCA5 | Analog input | H-bridge `CURRENT6` ← filtered MC33926 `FB`, 0–3.3 V only | TBD by continuity |
| All motors | `MOTOR_EN` | HSEC71 — GPIO18 | Output | External safety gate, then H-bridge `EN1`–`EN6` | TBD by continuity |
| Encoder 1 / axis 0 | `ENC1_A` | HSEC68 — GPIO20 / eQEP1A | Input | Encoder PCB `ENC1_A` → optional U1 B1; U1 A1 → HSEC68 | TBD by continuity |
| Encoder 1 / axis 0 | `ENC1_B` | HSEC70 — GPIO21 / eQEP1B | Input | Encoder PCB `ENC1_B` → optional U1 B2; U1 A2 → HSEC70 | TBD by continuity |
| Encoder 1 / axis 0 | `ENC1_I` | HSEC74 — GPIO23 / eQEP1I | Input | Encoder PCB `ENC1_I` → optional U1 B3; U1 A3 → HSEC74 | TBD by continuity |
| Encoder 2 / axis 1 | `ENC2_A` | HSEC100 — GPIO54 / eQEP2A | Input | Encoder PCB `ENC2_A` → optional U1 B4; U1 A4 → HSEC100 | TBD by continuity |
| Encoder 2 / axis 1 | `ENC2_B` | HSEC102 — GPIO55 / eQEP2B | Input | Encoder PCB `ENC2_B` → optional U1 B5; U1 A5 → HSEC102 | TBD by continuity |
| Encoder 2 / axis 1 | `ENC2_I` | HSEC106 — GPIO57 / eQEP2I | Input | Encoder PCB `ENC2_I` → optional U1 B6; U1 A6 → HSEC106 | TBD by continuity |
| Encoder 3 / axis 2 | `ENC3_A` | HSEC127 — GPIO62 / eQEP3A | Input | Encoder PCB `ENC3_A` → optional U1 B7; U1 A7 → HSEC127 | TBD by continuity |
| Encoder 3 / axis 2 | `ENC3_B` | HSEC128 — GPIO63 / eQEP3B | Input | Encoder PCB `ENC3_B` → optional U1 B8; U1 A8 → HSEC128 | TBD by continuity |
| Encoder 3 / axis 2 | `ENC3_I` | HSEC130 — GPIO65 / eQEP3I | Input | Encoder PCB `ENC3_I` → optional U2 B1; U2 A1 → HSEC130 | TBD by continuity |
| Encoder 4 / axis 3 | `ENC4_A` | HSEC75 — GPIO24 / CLB1 | Input | Encoder PCB `ENC4_A` → optional U2 B2; U2 A2 → HSEC75 | TBD by continuity |
| Encoder 4 / axis 3 | `ENC4_B` | HSEC77 — GPIO25 / CLB1 | Input | Encoder PCB `ENC4_B` → optional U2 B3; U2 A3 → HSEC77 | TBD by continuity |
| Encoder 5 / axis 4 | `ENC5_A` | HSEC79 — GPIO26 / CLB2 | Input | Encoder PCB `ENC5_A` → optional U2 B4; U2 A4 → HSEC79 | TBD by continuity |
| Encoder 5 / axis 4 | `ENC5_B` | HSEC81 — GPIO27 / CLB2 | Input | Encoder PCB `ENC5_B` → optional U2 B5; U2 A5 → HSEC81 | TBD by continuity |
| Encoder 6 / axis 5 | `ENC6_A` | HSEC80 — GPIO30 / CLB3 | Input | Encoder PCB `ENC6_A` → optional U2 B6; U2 A6 → HSEC80 | TBD by continuity |
| Encoder 6 / axis 5 | `ENC6_B` | HSEC88 — GPIO39 / CLB3 | Input | Encoder PCB `ENC6_B` → optional U2 B7; U2 A7 → HSEC88 | TBD by continuity |
| Pi SPI | MOSI | HSEC108 — GPIO58 / SPIA SIMO | Input | Raspberry Pi physical 19 / BCM10 | — |
| Pi SPI | MISO | HSEC110 — GPIO59 / SPIA SOMI | Output | Raspberry Pi physical 21 / BCM9 | — |
| Pi SPI | SCLK | HSEC125 — GPIO60 / SPIA CLK | Input | Raspberry Pi physical 23 / BCM11 | — |
| Pi SPI | CE0 / STE | HSEC126 — GPIO61 / SPIA STE | Input | Raspberry Pi physical 24 / BCM8 | — |
| Pi SPI | CE0 mirror | HSEC89 — GPIO40 / XINT5 | Input | Branch the same Raspberry Pi physical 24 / BCM8 wire | — |
| Limit | `LIMIT0_N` | HSEC141 — GPIO74 | Input | Conditioned 3.3 V limit input 0 | — |
| Limit | `LIMIT1_N` | HSEC142 — GPIO75 | Input | Conditioned 3.3 V limit input 1 | — |
| Limit | `LIMIT2_N` | HSEC143 — GPIO76 | Input | Conditioned 3.3 V limit input 2 | — |
| Limit | `LIMIT3_N` | HSEC144 — GPIO77 | Input | Conditioned 3.3 V limit input 3 | — |
| Limit | `LIMIT4_N` | HSEC145 — GPIO78 | Input | Conditioned 3.3 V limit input 4 | — |
| Limit | `LIMIT5_N` | HSEC146 — GPIO79 | Input | Conditioned 3.3 V limit input 5 | — |
| Limit | `LIMIT6_N` | HSEC147 — GPIO80 | Input | Conditioned 3.3 V limit input 6 | — |
| Limit | `LIMIT7_N` | HSEC148 — GPIO81 | Input | Conditioned 3.3 V limit input 7 | — |
| Safety status | `ESTOP_OK` | HSEC149 — GPIO82 | Input | 3.3 V status contact/output from independent E-stop circuit | — |
| Future encoder 0 | `SLOW0_A` | HSEC129 — GPIO64 | Input | Reserved; do not connect in first pass | — |
| Future encoder 0 | `SLOW0_B` | HSEC131 — GPIO66 | Input | Reserved; do not connect in first pass | — |
| Future encoder 1 | `SLOW1_A` | HSEC132 — GPIO67 | Input | Reserved; do not connect in first pass | — |
| Future encoder 1 | `SLOW1_B` | HSEC133 — GPIO68 | Input | Reserved; do not connect in first pass | — |
| Future encoder 2 | `SLOW2_A` | HSEC134 — GPIO69 | Input | Reserved; do not connect in first pass | — |
| Future encoder 2 | `SLOW2_B` | HSEC137 — GPIO70 | Input | Reserved; do not connect in first pass | — |
| Future encoder 3 | `SLOW3_A` | HSEC138 — GPIO71 | Input | Reserved; do not connect in first pass | — |
| Future encoder 3 | `SLOW3_B` | HSEC140 — GPIO73 | Input | Reserved; do not connect in first pass | — |
| Auxiliary ADC | `AUX_ADC0` | HSEC12 — ADCB0 | Analog input | Conditioned external analog 0, 0–3.3 V | — |
| Auxiliary ADC | `AUX_ADC1` | HSEC14 — ADCB1 | Analog input | Conditioned external analog 1, 0–3.3 V | — |
| Auxiliary ADC | `AUX_ADC2` | HSEC18 — ADCB2 | Analog input | Conditioned external analog 2, 0–3.3 V | — |
| Auxiliary ADC | `AUX_ADC3` | HSEC20 — ADCB3 | Analog input | Conditioned external analog 3, 0–3.3 V | — |
| Auxiliary ADC | `AUX_ADC4` | HSEC24 — ADCB4 | Analog input | Reserved conditioned analog 4, 0–3.3 V | — |
| Auxiliary ADC | `AUX_ADC5` | HSEC26 — ADCB5 | Analog input | Reserved conditioned analog 5, 0–3.3 V | — |
| Auxiliary ADC | `AUX_ADC6` | HSEC31 — ADCC2 | Analog input | Reserved conditioned analog 6, 0–3.3 V | — |
| Auxiliary ADC | `AUX_ADC7` | HSEC33 — ADCC3 | Analog input | Reserved conditioned analog 7, 0–3.3 V | — |
| Logic ground | `GND` | HSEC135 and/or HSEC157 — GND | — | Pi physical 6, translator GND, encoder-PCB logic GND, and H-bridge logic GND | — |

Do not route motor return current through the Pi or dock jumper ground. Join the
logic reference to the motor-power ground at the intended point on the H-bridge
PCB/power system.

## 2. HEDL-5540 connector to encoder PCB

Repeat this 10-pin connector wiring for each encoder. The existing PCB's MC3486
receives the differential pairs and produces the logical `ENC<n>_A/B/I` outputs
used above.

| HEDL connector pin | Wire color | Encoder signal | Connect to encoder PCB |
|---:|---|---|---|
| 1 | Brown | NC | Leave open |
| 2 | Red | `+5 V` | Encoder-PCB regulated 5 V |
| 3 | Orange | GND | Encoder-PCB logic ground |
| 4 | Yellow | NC | Leave open |
| 5 | Green | `A+` | Receiver channel A non-inverting input |
| 6 | Blue | `A−` | Receiver channel A inverting input |
| 7 | Violet | `B+` | Receiver channel B non-inverting input |
| 8 | Grey | `B−` | Receiver channel B inverting input |
| 9 | White | `I+` | Receiver index non-inverting input |
| 10 | Black | `I−` | Receiver index inverting input |

Axes 3–5 do not have DSP index inputs in the provisional map; their receiver
index outputs remain unconnected.

## 3. Optional TXS0108E prototype wiring

Use this only after measured MC3486 levels satisfy the TXS0108E input thresholds.
If direct connection is validated instead, bypass U1/U2 but keep the same signal
order. One eight-channel module is not enough: 15 connected encoder signals need
two modules.

| Module pin/group | Connect to |
|---|---|
| U1/U2 `VCCA` | Regulated 3.3 V, for example Pi physical 1 |
| U1/U2 `VCCB` | The same regulated 5 V rail that powers the MC3486 outputs; do not tie two independent 5 V supplies together |
| U1/U2 `GND` | Common logic ground |
| U1/U2 `OE` | Common manual enable: 10 kΩ pull-down to GND, then jumper to 3.3 V only after both rails are stable |
| U1 B1–B8 | Encoder-PCB outputs `ENC1_A`, `ENC1_B`, `ENC1_I`, `ENC2_A`, `ENC2_B`, `ENC2_I`, `ENC3_A`, `ENC3_B` |
| U1 A1–A8 | DSP connections listed for those signals in Section 1 |
| U2 B1–B7 | Encoder-PCB outputs `ENC3_I`, `ENC4_A`, `ENC4_B`, `ENC5_A`, `ENC5_B`, `ENC6_A`, `ENC6_B` |
| U2 A1–A7 | DSP connections listed for those signals in Section 1 |
| U2 A8/B8 | Spare; leave open |

## 4. MC33926 local power and fixed wiring

Repeat for each bridge channel. Verify that these connections already exist on
the custom PCB before adding jumpers.

| MC33926 connection | Connect to |
|---|---|
| `VPWR` | Current-limited 24 V motor supply |
| `PGND/GND` | Motor supply ground at the H-bridge PCB |
| `OUT1`, `OUT2` | Maxon motor terminals; final positive direction is set by `motor_polarity` |
| `IN2` | Logic low |
| `D2` | Logic high |
| `SLEW` | Logic high for 20 kHz target; use at most 10 kHz if tied low |
| `D1` | Corresponding `DISABLE<n>` plus external pull-up to 3.3 V |
| `EN` | Gated shared `MOTOR_EN` plus external pull-down to GND |
| `SF` | Corresponding `FAULT<n>_N` plus pull-up to 3.3 V |
| `FB` | 270 Ω starting resistor/filter, then corresponding DSP ADC input |

## 5. Pre-power checklist

- [ ] Custom-PCB connector numbers are filled in for every used logical signal.
- [ ] HSEC pin orientation is confirmed against the dock silkscreen/schematic.
- [ ] No encoder, fault, limit, E-stop, or ADC input can exceed 3.3 V.
- [ ] Pi, DSP, translator, encoder PCB, and H-bridge logic share a reference.
- [ ] Motor-current return does not flow through logic-ground jumpers.
- [ ] CE0 reaches both HSEC126 and HSEC89.
- [ ] `DISABLE1`–`DISABLE6` are high and `MOTOR_EN` is low before power-up.
- [ ] First motor test uses one axis, 2% duty, and a 0.5 A supply limit.

