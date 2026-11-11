# Electronics and DSP Pin Assignment

Status: existing hardware reference plus proposed expansion allocations

Last updated: 2026-11-10 (pin assignments retained from 2026-08-05)

Two-axis operation is established by the owner's bench result. This document
combines board constraints with planned six-axis, auxiliary-encoder, and switch
allocations; it is not evidence that every listed feature is implemented or wired.
See [current system](CURRENT_SYSTEM.md) for implementation status and
[wiring_table.md](wiring_table.md) for literal dock hookups.

Protocol v5 uses three eQEP and three CPU2-polled motor encoder channels.
General I/O is 4 DI / 4 DO / 6 AI / 2 AO through direct dock wiring. The
[v5 allocation](wiring_table.md#protocol-v5-direct-dock-io) overrides older
reservations below: ADCA0/1 are DAC outputs, ADCB0–5 are active analog inputs,
GPIO32/33/80/81 are DI and GPIO35–38 are DO. Home/limit and auxiliary encoder
allocations remain proposals. CPU2 uses GPIO polling, not eCAP interrupts.

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

The system is intended to support general brushed DC motors and A/B encoders.
These Maxon parts are the documented reference assembly, not firmware constants.

| Component | Part | First-pass data |
|---|---|---|
| Motor | Maxon 148867 | RE 40, 24 V, 150 W |
| Gearhead | Maxon 203116 | absolute ratio `91/6` |
| Encoder | Maxon 110514 / HEDL-5540 | 500 cycles/turn, differential A/B/index |
| Working axes 0/1 transmission | Rack and pinion | 30 teeth, module 1 mm; gearbox output drives pinion |

The owner confirmed the encoder/gearbox parameters for both working axes on
2026-11-10. They are linear actuators. Pinion pitch diameter is 30 mm and travel
per output revolution is `30π` mm. Nominal scale is
`(500 × 4 × (91/6)) / (30π) = 321.8466626969` counts/mm. The first UI uses a
−50 to +50 mm range on both axes; see [system design](ROS2_plan.md) for zero and
conversion behavior. This calculation is not a measured backlash/scale test.

The motor's 6 A continuous rating exceeds the MC33926's nominal 5 A rating and
the current PCB has no current-feedback output. Treat the bridge as derated:
start one-axis tests with a 0.5 A bench-supply limit and 2% duty.

| Quantity | Motor axes 0–5 (`×4`) | Auxiliary sensors 0–3 (`×2`) |
|---|---:|---:|
| Counts per encoder revolution at 500 CPT | 2,000 | 1,000 |
| Counts per geared output revolution at ratio `91/6` | 30,333.333... | profile-dependent |
| Motor event rate at 12,000 rpm | 400,000 transitions/s per CPU2-decoded motor axis | profile-dependent |

Axes 0–2 use hardware eQEP at ×4. Axes 3–5 now use CPU2 GPIO polling at ×4,
with three-sample input qualification. The earlier six-eCAP interrupt approach
is superseded. Four auxiliary decoders remain deferred.

At 8,000 rpm each 500-CPT motor encoder produces approximately 266,667 edges/s
(3.75 µs spacing); at 12,000 rpm this is 400,000 edges/s (2.5 µs spacing).
The observed CPU2 polling gap is reported in telemetry, but idle-pin timing is
not proof of counting accuracy under simultaneous external edges. Rate acceptance
requires external stimulus on all three channels and comparison with known counts.

The current DSP reports native counts and accepts normalized duty. Stage 2 adds
Pi conversion using configured scale, transmission, direction, and zero so the
UI controls calibrated degrees/mm immediately (mm for the two working axes).
Standard ROS joint interfaces, if used, retain radians/meters internally.
Other axes still require their own confirmed transmission parameters.

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
| `PWM1`–`PWM6` | ePWM1A–ePWM6A | 20 kHz duty magnitude; existing direction/duty update behavior |
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
| 0 | GPIO0 / HSEC49 / ePWM1A | GPIO1 / HSEC51 | GPIO12 / HSEC58 | Reassigned to DACA / AO0 |
| 1 | GPIO2 / HSEC53 / ePWM2A | GPIO3 / HSEC55 | GPIO13 / HSEC60 | Reassigned to DACB / AO1 |
| 2 | GPIO4 / HSEC50 / ePWM3A | GPIO5 / HSEC52 | GPIO14 / HSEC62 | ADCA2 / HSEC15 |
| 3 | GPIO6 / HSEC54 / ePWM4A | GPIO7 / HSEC56 | GPIO15 / HSEC64 | ADCA3 / HSEC17 |
| 4 | GPIO8 / HSEC57 / ePWM5A | GPIO9 / HSEC59 | GPIO16 / HSEC67 | ADCA4 / HSEC21 |
| 5 | GPIO10 / HSEC61 / ePWM6A | GPIO11 / HSEC63 | GPIO17 / HSEC69 | ADCA5 / HSEC23 |

Shared `MOTOR_EN` is GPIO18 / HSEC71. ADCA2–5 remain reserved for future motor-current
feedback. ADCA0/1 now serve DAC outputs. This does not prevent
stage-3 direct-dock tests of separately assigned analog channels.

### 5.2 Motor encoders

| Axis | A | B | Index | Decoder / owner | Resolution |
|---:|---|---|---|---|---:|
| 0 | GPIO20 / HSEC68 | GPIO21 / HSEC70 | GPIO23 / HSEC74 | eQEP1 / CPU1 | `×4` |
| 1 | GPIO54 / HSEC100 | GPIO55 / HSEC102 | GPIO57 / HSEC106 | eQEP2 / CPU1 | `×4` |
| 2 | GPIO62 / HSEC127 | GPIO63 / HSEC128 | GPIO65 / HSEC130 | eQEP3 / CPU1 | `×4` |
| 3 | GPIO24 / HSEC75 / CPU2 GPIO polling | GPIO25 / HSEC77 / CPU2 GPIO polling | none | CPU2 polling, A/B transitions | `×4` |
| 4 | GPIO26 / HSEC79 / CPU2 GPIO polling | GPIO27 / HSEC81 / CPU2 GPIO polling | none | CPU2 polling, A/B transitions | `×4` |
| 5 | GPIO30 / HSEC80 / CPU2 GPIO polling | GPIO39 / HSEC88 / CPU2 GPIO polling | none | CPU2 polling, A/B transitions | `×4` |

Index on axes 0–2 is diagnostic only; it does not automatically reset position.
Stage 2 uses the measured startup position as zero. Future homing will establish
a machine reference. Axes 3–5 are A/B-only.

### 5.3 Auxiliary encoders

| Sensor | A | B | Decoder / owner | Resolution |
|---:|---|---|---|---:|
| 0 | GPIO64 / HSEC129 / XBAR4 → XINT1 | GPIO66 / HSEC131 | CPU2, both A edges/sample B | `×2` |
| 1 | GPIO67 / HSEC132 / XBAR5 → XINT2 | GPIO68 / HSEC133 | CPU2, both A edges/sample B | `×2` |
| 2 | GPIO69 / HSEC134 / XBAR6 → XINT3 | GPIO70 / HSEC137 | CPU2, both A edges/sample B | `×2` |
| 3 | GPIO71 / HSEC138 / XBAR13 → XINT4 | GPIO73 / HSEC140 | CPU2, both A edges/sample B | `×2` |

The motor-encoder CPU2 implementation publishes counts and diagnostics to
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

### 5.5 Proposed homing switches (not implemented)

| Axis | Signal | GPIO / dock | Handling |
|---:|---|---|---|
| 0 | `HOME0_N` | GPIO74 / HSEC141 | CPU1 5 kHz polling, internal pull-up |
| 1 | `HOME1_N` | GPIO75 / HSEC142 | CPU1 5 kHz polling, internal pull-up |
| 2 | `HOME2_N` | GPIO76 / HSEC143 | CPU1 5 kHz polling, internal pull-up |
| 3 | `HOME3_N` | GPIO77 / HSEC144 | CPU1 5 kHz polling, internal pull-up |
| 4 | `HOME4_N` | GPIO78 / HSEC145 | CPU1 5 kHz polling, internal pull-up |
| 5 | `HOME5_N` | GPIO79 / HSEC146 | CPU1 5 kHz polling, internal pull-up |

The earlier proposed active-low profile wires a normally-open switch from GPIO
to logic ground: open high, pressed low. The new requirement is separately
configurable input polarity plus min/max switches. These six home assignments
are reference allocations, not a completed allocation for all required inputs.
GPIO80/81 now serve general DI; GPIO82 remains spare.

The proposed homing sequence is move away if active, seek, back off until
released, reapproach slowly, and establish zero. Pi-side sequencing is accepted for later work in [the system design](ROS2_plan.md). Do not implement the former DSP-owned
sequence solely because this hardware table describes home inputs.

### 5.6 General analog and digital I/O

ADCB0–5 (HSEC12/14/18/20/24/26) are active 12-bit analog inputs. DACA/B
(HSEC9/11) replace the unused ADCA0/1 current-feedback reservations. They use
ADCA VREFHI; ADCB has its own reference. UI and wire values remain raw counts
until the two controlCARD SW2 reference positions are confirmed. The
[wiring table](wiring_table.md#protocol-v5-direct-dock-io) gives all 4 DI / 4 DO /
6 AI / 2 AO pins, electrical ranges, stop values and loopback connections.
ADCA2–5 and ADCC2–3 remain reserved, unconfigured inputs.

## 6. Current peripheral budget

| Resource | Active use | Remaining / gate |
|---|---|---|
| eQEP | 1–3, motor axes 0–2 | all used |
| eCAP | none | available; prior interrupt allocation superseded |
| External interrupts | XINT5 for SPI CE mirror | XINT1–4 unused |
| Input X-BAR | input 14 for CE mirror | other inputs unused by current build |
| CPU2 | dedicated three-channel quadrature polling | external rate test pending |
| ePWM | 1A–6A motor PWM | B outputs used as GPIO/STBY |
| General digital | GPIO32/33/80/81 DI; GPIO35–38 DO | GPIO82 spare |
| ADC | ADCB0–5 | other analog channels unconfigured |
| DAC | DACA/B on ADCA0/1 | DACC unused |
| CLA / CLB | none in active endpoint | available |

## 7. Expansion checks (historical checklist, not a fresh two-axis blocker)

These unchecked entries are not a current bench test report. Keep the accepted
two-axis baseline; complete relevant checks when wiring or enabling additional
channels. Do not mark an unmeasured channel tested merely because two axes work.

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
