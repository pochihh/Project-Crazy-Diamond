# Electronics and DSP Pin Assignment

Status: locked prototype pin map; complete continuity and rate gates before
energized multi-axis tests

Last updated: 2026-08-05

This is the authoritative hardware interface for the Raspberry Pi 5,
TMS320F28379D controlCARD, six current MC33926 boards, six motor encoders, four
optional encoder-only sensors, and six homing switches. Literal dock hookups are
in [wiring_table.md](wiring_table.md).

## 1. Electrical architecture

```text
encoder differential A/B(/I)
        │
        ▼
MC3486 receiver ── 5 V logic ──► tested level shifter ── 3.3 V ──► DSP GPIO

Pi 5 ◄──────────── SPIA, 10 MHz baseline ───────────────────────► DSP

DSP PWM/DIR/STBY/shared EN ──► custom MC33926 boards ──► motors
```

The external E-stop is entirely physical and is neither monitored nor controlled
by the DSP. It must remove motor energy or gate the bridge independently of the
Pi, ROS 2, and DSP software.

## 2. Reference axis and units

The software supports general brushed DC motors and A/B encoders. These Maxon
parts are the initial configuration, not firmware constants.

| Component | Part | First-pass data |
|---|---|---|
| Motor | Maxon 148867 | RE 40, 24 V, 150 W |
| Gearhead | Maxon 203116 | absolute ratio `91/6` |
| Encoder | Maxon 110514 / HEDL-5540 | 500 cycles/turn, differential A/B/index |

The motor's 6 A continuous rating exceeds the MC33926's nominal 5 A rating and
the current PCB has no current-feedback output. Treat the bridge as derated:
start one-axis tests with a 0.5 A bench-supply limit and 2% duty.

| Quantity | Motor axes 0–5 (`×4`) | Auxiliary sensors 0–3 (`×2`) |
|---|---:|---:|
| Counts per encoder revolution at 500 CPT | 2,000 | 1,000 |
| Counts per geared output revolution at ratio `91/6` | 30,333.333... | profile-dependent |
| Motor event rate at 12,000 rpm | 400,000 interrupts/s per CPU2-decoded motor axis | profile-dependent |

Axes 0–2 use hardware eQEP at `×4`. Axes 3–5 interrupt on both A and B edges
through eCAP1–6 and also produce `×4`. Auxiliary encoders interrupt on both A
edges and sample B, producing `×2`.

At the gearhead's 8,000 rpm continuous input limit, axes 3–5 can generate about
800,000 CPU2 interrupts/s in aggregate before auxiliary traffic. At the motor's
12,000 rpm mechanical limit they can generate 1.2 million/s. CPU2 has no other
real-time job, but this remains an explicit simultaneous-rate acceptance gate;
it is not assumed to fit from cycle estimates alone.

The DSP uses only native counts and counts/second. The Pi converts geared-output
radians and radians/second using configured encoder scale, gear ratio, sign, and
zero.

## 3. Current MC33926 PCB contract

The exact schematic is unavailable, but continuity established:

- Native `EN` reaches PCB `EN`.
- Native `D2` reaches PCB `STBY`.
- Native `D1` is grounded.
- PCB `PWM` and `DIR` pass through intermediate logic; use them as the board
  contract without assuming the hidden IN1/IN2 truth table.
- Powered-board measurement puts `SLEW` at 3.3 V (fast slew), permitting 20 kHz PWM.
- `SF` and `FB` are not exposed on the current PCB.

| Board signal | DSP resource | Required behavior |
|---|---|---|
| `PWM1`–`PWM6` | ePWM1A–ePWM6A | 20 kHz duty magnitude; zero before direction changes |
| `DIR1`–`DIR6` | six GPIO outputs | Per-axis direction |
| `STBY1`–`STBY6` | six GPIO outputs | Low disables/stands by that axis; high permits drive |
| `MOTOR_EN` | one GPIO output branched to all six `EN` inputs | Low disables all axes; high permits operation |

Boot/disarm/software-E-stop state is PWM = 0, all six `STBY` low, and shared
`MOTOR_EN` low. Software E-stop is latched and requires explicit clear followed
by re-arm. Arm raises shared EN only from the safe state, waits the verified
board wake interval, then permits selected axes with their STBY lines. Direction
changes occur only at zero duty after at least one PWM period.

The MC33926 digital inputs accept the DSP's 3.3 V high level; no bridge-control
level shifter is required for the current board contract.

## 4. Encoder electrical interface

The HEDL-5540 produces differential RS-422 signals. The existing PCB receives
them with MC3486 devices and exposes 5 V TTL-compatible logic. TTL-compatible
does not mean 3.3 V-safe: the guaranteed high threshold/level relationship does
not prevent the actual MC3486 output from approaching its 5 V supply.

F28379D GPIO is not 5 V tolerant. Do not connect MC3486 outputs directly.

Prototype decision: use the bench-tested MC3486 → TXS0108E → DSP chain, with
`VCCA = 3.3 V`, `VCCB = 5 V`, common ground, and OE held low until both rails are
stable. Keep wiring short and do not add strong pull resistors or capacitive
loads to the signal pins. A 2026-08-05 test produced a clean 3.3 V waveform.

This remains a prototype exception: at `VCCB = 5 V`, TXS0108E's guaranteed input
high requirement is higher than MC3486's guaranteed TTL high output. The
measured combination works but is not guaranteed across all devices and
temperatures. Scope all 23 connected A/B/index channels at maximum configured
rates before multi-axis use.

A stationary A or B may be high or low; it represents rotor phase. During
rotation A and B must be equal-frequency square waves in quadrature. One brief
pulse per revolution is probably index, not A or B.

For the next PCB, replace the MC3486/translator chain with 3.3 V `AM26LV32E`
quad RS-422 receivers, 100–120 Ω termination at each receiver, local 0.1 µF
decoupling, and a signal reference. The present 23 receiver channels require six
quad devices.

## 5. DSP pin assignment

`HSEC` is the dock connector pin. Axes 0–2 are locked because they are already
wired.

### 5.1 Motor channels and future current inputs

| Axis | PWM | STBY | DIR | Future current input |
|---:|---|---|---|---|
| 0 | GPIO0 / HSEC49 / ePWM1A | GPIO1 / HSEC51 | GPIO12 / HSEC58 | ADCA0 / HSEC9 |
| 1 | GPIO2 / HSEC53 / ePWM2A | GPIO3 / HSEC55 | GPIO13 / HSEC60 | ADCA1 / HSEC11 |
| 2 | GPIO4 / HSEC50 / ePWM3A | GPIO5 / HSEC52 | GPIO14 / HSEC62 | ADCA2 / HSEC15 |
| 3 | GPIO6 / HSEC54 / ePWM4A | GPIO7 / HSEC56 | GPIO15 / HSEC64 | ADCA3 / HSEC17 |
| 4 | GPIO8 / HSEC57 / ePWM5A | GPIO9 / HSEC59 | GPIO16 / HSEC67 | ADCA4 / HSEC21 |
| 5 | GPIO10 / HSEC61 / ePWM6A | GPIO11 / HSEC63 | GPIO17 / HSEC69 | ADCA5 / HSEC23 |

Shared `MOTOR_EN` is GPIO18 / HSEC71. ADCA0–5 are reserved for a future PCB and
remain disabled in the current firmware/configuration.

### 5.2 Motor encoders

| Axis | A | B | Index | Decoder / owner | Resolution |
|---:|---|---|---|---|---:|
| 0 | GPIO20 / HSEC68 | GPIO21 / HSEC70 | GPIO23 / HSEC74 | eQEP1 / CPU1 | `×4` |
| 1 | GPIO54 / HSEC100 | GPIO55 / HSEC102 | GPIO57 / HSEC106 | eQEP2 / CPU1 | `×4` |
| 2 | GPIO62 / HSEC127 | GPIO63 / HSEC128 | GPIO65 / HSEC130 | eQEP3 / CPU1 | `×4` |
| 3 | GPIO24 / HSEC75 / XBAR7 → eCAP1 | GPIO25 / HSEC77 / XBAR8 → eCAP2 | none | CPU2, both A/B edges | `×4` |
| 4 | GPIO26 / HSEC79 / XBAR9 → eCAP3 | GPIO27 / HSEC81 / XBAR10 → eCAP4 | none | CPU2, both A/B edges | `×4` |
| 5 | GPIO30 / HSEC80 / XBAR11 → eCAP5 | GPIO39 / HSEC88 / XBAR12 → eCAP6 | none | CPU2, both A/B edges | `×4` |

Index on axes 0–2 is diagnostic only; it does not automatically reset position.
Homing defines axis zero. Axes 3–5 are A/B-only in this pass.

### 5.3 Auxiliary encoders

| Sensor | A | B | Decoder / owner | Resolution |
|---:|---|---|---|---:|
| 0 | GPIO64 / HSEC129 / XBAR4 → XINT1 | GPIO66 / HSEC131 | CPU2, both A edges/sample B | `×2` |
| 1 | GPIO67 / HSEC132 / XBAR5 → XINT2 | GPIO68 / HSEC133 | CPU2, both A edges/sample B | `×2` |
| 2 | GPIO69 / HSEC134 / XBAR6 → XINT3 | GPIO70 / HSEC137 | CPU2, both A edges/sample B | `×2` |
| 3 | GPIO71 / HSEC138 / XBAR13 → XINT4 | GPIO73 / HSEC140 | CPU2, both A edges/sample B | `×2` |

CPU2 publishes all seven software-decoded encoder counts and diagnostics to
CPU1 at 5 kHz through CPU2→CPU1 message RAM. It writes an inactive snapshot
slot, completes its sequence guard, then switches the active slot. CPU1 validates
the slot and sequence at the control boundary. IPC flags are for boot,
configuration, zero/acknowledgement, and liveness—not per encoder edge.

### 5.4 Raspberry Pi SPI

| Signal | Raspberry Pi 5 | DSP |
|---|---|---|
| MOSI | BCM10 / physical 19 | GPIO58 / HSEC108 / SPIA SIMO |
| MISO | BCM9 / physical 21 | GPIO59 / HSEC110 / SPIA SOMI |
| SCLK | BCM11 / physical 23 | GPIO60 / HSEC125 / SPIA CLK |
| CE0 | BCM8 / physical 24 | GPIO61 / HSEC126 / SPIA STE and GPIO40 / HSEC89 / XBAR14 → XINT5 |
| GND | physical 6 or another ground | common logic ground |

### 5.5 Homing switches

| Axis | Signal | GPIO / dock | Handling |
|---:|---|---|---|
| 0 | `HOME0_N` | GPIO74 / HSEC141 | CPU1 5 kHz polling, internal pull-up |
| 1 | `HOME1_N` | GPIO75 / HSEC142 | CPU1 5 kHz polling, internal pull-up |
| 2 | `HOME2_N` | GPIO76 / HSEC143 | CPU1 5 kHz polling, internal pull-up |
| 3 | `HOME3_N` | GPIO77 / HSEC144 | CPU1 5 kHz polling, internal pull-up |
| 4 | `HOME4_N` | GPIO78 / HSEC145 | CPU1 5 kHz polling, internal pull-up |
| 5 | `HOME5_N` | GPIO79 / HSEC146 | CPU1 5 kHz polling, internal pull-up |

Wire each normally-open switch from its GPIO directly to logic ground. Open is
high and reached/closed is low. There is no shared `LIMIT_ANY`, no limit ISR,
and no additional limit function in this pass. GPIO80–82 remain spares.

Homing runs one axis at a time: move away if initially active, seek, back off
until released, reapproach slowly, then latch the raw count/offset. Timeout,
maximum travel, command watchdog, or software E-stop aborts and disarms.

### 5.6 Reserved auxiliary analog inputs

| Channel | Pin | First-pass state |
|---:|---|---|
| 0 | ADCB0 / HSEC12 | reserved, disabled |
| 1 | ADCB1 / HSEC14 | reserved, disabled |
| 2 | ADCB2 / HSEC18 | reserved, disabled |
| 3 | ADCB3 / HSEC20 | reserved, disabled |
| 4 | ADCB4 / HSEC24 | reserved, disabled |
| 5 | ADCB5 / HSEC26 | reserved, disabled |
| 6 | ADCC2 / HSEC31 | reserved, disabled |
| 7 | ADCC3 / HSEC33 | reserved, disabled |

The protocol reserves telemetry space, but disabled channels report invalid/zero
and no ADC converter/SOC is enabled for them.

## 6. Peripheral and GPIO budget

| Resource | Use | Remaining / gate |
|---|---|---|
| eQEP | eQEP1–3 for axes 0–2 | all used |
| eCAP | eCAP1–6 for both edges of axes 3–5 | all used |
| External interrupts | XINT1–4 auxiliary encoders; XINT5 SPI CE mirror | all five used |
| Input X-BAR | 4–6 auxiliary; 7–12 motor encoders; 13 auxiliary; 14 CE mirror | inputs 1–3 free |
| CPU2 interrupt sources | eCAP1–6 plus XINT1–4 | rate test required |
| ePWM | ePWM1A–6A motor PWM | B outputs unused as PWM |
| Digital GPIO | 53 connected signals | GPIO32, 33, 35–38, 80–82 reserved/spare |
| ADC | ADCA0–5 plus ADCB0–5/ADCC2–3 reserved | all disabled in current hardware profile |
| CLB | none | four tiles available only as a measured fallback |

## 7. Hardware gates

- [ ] Trace each custom-PCB `PWM`, `DIR`, `STBY`, and `EN` connector position.
- [x] Confirm PCB EN → native EN, PCB STBY → native D2, and native D1 grounded.
- [x] Measure powered `SLEW` at 3.3 V (fast) and set PWM to 20 kHz.
- [x] Verify one MC3486/TXS0108E channel produces a clean 3.3 V waveform.
- [ ] Scope all 23 translated encoder channels over the required rates.
- [ ] Verify every selected HSEC pin against continuity and the controlCARD revision.
- [ ] Prove eCAP1–6 `×4` plus XINT1–4 `×2` simultaneously with zero lost events
      and measured CPU2 headroom.
- [ ] Verify each home input is high open/low pressed with the internal pull-up.
- [ ] Bench-test the independent physical E-stop circuit.
- [ ] Start energized testing on one axis at 2% duty and 0.5 A supply limit.

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
