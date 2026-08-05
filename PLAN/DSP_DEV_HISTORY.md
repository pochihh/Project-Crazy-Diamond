# DSP_28379D Development Plan

> **Historical implementation record.** This file describes the currently
> validated legacy DMA example, including its old frame, GPIO123/XINT1 boundary,
> and earlier loop-rate assumptions. It is not the target interface. Use
> [`electronics.md`](electronics.md) for pins and electrical decisions,
> [`spi_protocol.md`](spi_protocol.md) for protocol v4, and
> [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) for migration
> gates. Preserve the tests here while replacing the obsolete interface.

## Overview

Replace CPU-driven SPI (ISR per word) from `SPI_Slave_Motor_Control` with DMA-driven SPI for reliable high-speed communication. Target: 10MHz SPICLK, ~5kHz control loop.

**Priority: DMA SPI is the critical path** — everything else depends on reliable communication.

---

## Architecture

**Full-duplex SPI slave using 2 DMA channels:**
- DMA CH1: SPIA RX (trigger 109) — SPI RX FIFO → RAM buffer
- DMA CH2: SPIA TX (trigger 110) — RAM buffer → SPI TX FIFO
- CS interrupt (GPIO 123 → XINT1) for frame boundary detection
- Double-buffered TX/RX: CPU works on standby buffer while DMA uses active buffer

**Key difference from old design:** No CPU involvement during SPI transfer. DMA handles all FIFO reads/writes. CPU only acts on CS edges.

---

## Phase 1: DMA SPI — COMPLETE ✓

Validated at 20 MHz SPICLK, 100% valid frame rate across Phase A/B/C.

### Files

| File | Status | Description |
|------|--------|-------------|
| `spi_dma.h` | DONE | Production driver API (no test code) |
| `spi_dma.c` | DONE | Production driver — slave mode, rx callback, cs debounce |
| `tests/spi_dma_test.h` | DONE | Test harness header (was spi_dma.h) |
| `tests/spi_dma_test.c` | DONE | Test harness with Phase A/B/C modes |
| `tests/rpi_spi_test.c` | DONE | RPi-side Linux spidev test program |
| `2837xD_RAM_lnk_cpu1.cmd` | DONE | DMA buffer section, code overflow, CLA prep |

### Phase C Bug Fix (exactly-half-fail)

**Root cause**: `cs_isr` fired twice per CS rising edge due to GPIO glitch or PIE
re-pending. Second invocation swapped to the just-cleared buffer (empty), causing
exactly 50% of frames to fail.

**Fix applied in both test and production code**:
1. `g_cs_active` state variable — falling edge sets it, rising edge clears it.
   Duplicate edges in either direction are counted in `cs_edge_ignored` and skipped.
2. `GPIO_QUAL_6SAMPLE` on GPIO 123 (CS mirror) — 30 ns glitch filter.
3. `validateRxFrame` in test code: Phase C uses RX frame format (0x55AA header,
   `RX_FRAME_WORDS-1 = 8` words for CRC). Phase A/B still uses TX frame format.

### SPI Configuration
- **Module**: SPIA on GPIO 58(SIMO)/59(SOMI)/60(CLK)/61(STE)
- **CS mirror**: GPIO 123 → XINT1 interrupt (both edges)
- **Mode**: Mode 3 (POL1PHA1) matching old project
- **Word size**: 16-bit
- **FIFO threshold**: RX=1, TX=1 (DMA triggers per word)
- **Target**: 10MHz SPICLK, 64-word frames → ~3.1kHz frame rate

### Frame Format (6-axis, v3 TX / v2 RX)

**TX (DSP → RPi): 61 words + 3 padding = 64 DMA words**
```
[0..3]   4x dummy (0xFFFF)
[4]      Header: 0xAA55
[5]      Version: 0x0003
[6..7]   timestamp_us (lo16, hi16)
[8..19]  ref[6] (float packed, 2 words each)
[20..31] pos[6] (int32 lo/hi, 2 words each)
[32..43] u[6] (float packed, 2 words each)
[44]     err_bitmap (bits 0-5 = axis 0-5 error flags)
[45]     err_count (uint16, cumulative total errors)
[46..51] adc[6] (uint16 each)
[52..59] quat[4] = {w,x,y,z} (float packed, 2 words each) — IMU quaternion
[60]     CRC16  (over words [4..59])
```

**RX (RPi → DSP): 17 words**
```
[0]      Header: 0x55AA
[1]      Version: 0x0002
[2..3]   cmd (uint32 lo/hi)
[4..15]  ref[6] (float packed, 2 words each)
[16]     CRC16  (over words [0..15])
```

**CRC**: CRC-16/CCITT-FALSE — poly 0x1021, init 0xFFFF, RefIn=false, RefOut=false.
Each 16-bit word fed high byte first (MSB-first), matching SPI transmission bit order.
Compatible with standard CRC-16/CCITT libraries. Implemented in software on both sides.

**DMA buffer sizing**: 4 buffers × 64 words × 2 bytes = 512 bytes = exactly RAMGS9_DMA.

### DMA Configuration

```
RX Channel (CH1):
  trigger:          DMA_TRIGGER_SPIARX (110)
  burstSize:        1 word
  transferSize:     MAX_FRAME_WORDS (64)
  src:              SPIA_BASE + SPI_O_RXBUF (fixed)
  dest:             rx_buffer[] (incrementing)
  mode:             ONE-SHOT (stop after frame, rearm in CS ISR)

TX Channel (CH2):
  trigger:          DMA_TRIGGER_SPIATX (109)
  burstSize:        1 word
  transferSize:     MAX_FRAME_WORDS (64)
  src:              tx_active_buffer[] (incrementing)
  dest:             SPIA_BASE + SPI_O_TXBUF (fixed)
  mode:             ONE-SHOT
```

### CS Interrupt Flow
```
CS falling (transfer start):
  - DMA already armed → nothing to do
  - Increment cs_falling_count

CS rising (transfer end):
  - Stop DMA channels
  - Swap RX buffers (g_rxDone now has received data)
  - Validate RX frame (header, version, CRC)
  - LED1 toggle on success, LED2 on error
  - Swap TX buffers if new data ready
  - Rearm DMA with fresh buffer addresses
  - Restart DMA channels
```

### Linker Memory Map Changes

```
PAGE 0 (code):
  RAMGS10_CODE  0x016000  4KB  .text overflow
  RAMGS11_CODE  0x017000  4KB  .text overflow
  RAMGS12_CODE  0x018000  4KB  .text overflow + .TI.ramfunc

PAGE 1 (data):
  RAMGS9_DMA    0x015000  512B  .dma_buffers (4 buffers × 30 words × 2B = 240B)
  RAMGS9        0x015200  3.5KB remaining general use
```

### Test Strategy

#### Phase A: Internal Loopback (DSP only, no wiring)

**Setup:**
1. In CCS, set `SPI_TEST_MODE` to `0` (or leave default)
2. Build RAM configuration, load via JTAG

**How it works:**
- SPI reconfigured as master + loopback enabled
- DMA TX feeds test pattern → loops back to DMA RX
- CPU verifies RX matches TX after each transfer
- Runs at ~100Hz (10ms delay between tests)

**What to check:**
- LED1 blinks steadily = frames passing
- LED2 off = no errors
- Watch variables in CCS:
  - `g_stats.tx_frame_count` — should increment
  - `g_stats.rx_frame_count` — should match tx count
  - `g_stats.crc_errors` — should be 0
  - `g_stats.frame_errors` — should be 0
  - `g_stats.dma_errors` — should be 0
  - `g_loopbackOk` — should increment
  - `g_loopbackErrors` — should be 0
  - `g_testPatternCounter` — incrementing pattern number

**Pass criteria:** 1000+ frames with 0 errors.

#### Phase B: External Loopback (MOSI→MISO jumper wire + RPi clock)

**Goal:** Validate GPIO signal path with DSP in **slave mode**. Frames only
increment when the RPi is connected and clocking — counter stays at 0 until
external master is present, making the test unambiguous.

**Wiring:**
```
GPIO 58 (SIMO) ──jumper──► GPIO 59 (SOMI)   [loopback wire]
RPi SCLK (GPIO 11) ──────► DSP GPIO 60 (CLK)
RPi CE0  (GPIO  8) ──────► DSP GPIO 61 (STE) + GPIO 123 (CS mirror)
RPi GND  ────────────────► DSP GND
```
RPi MOSI and MISO do NOT need to be connected — the jumper handles loopback.

**RPi command (low speed to start):**
```bash
sudo ./spi_test /dev/spidev0.0 1000000 100
```

**What to check on DSP (watch variables):**
- `g_stats.tx_frame_count` — stays 0 until RPi starts → confirms slave mode
- `g_stats.cs_rising_count` — increments with each RPi transfer
- `g_stats.crc_errors` = 0 (data through jumper matches expected)
- `g_stats.frame_errors` = 0

**Pass criteria:** frame_count matches RPi transfer count, crc_errors = 0.

#### Phase C: RPi Communication (full system test)

**DSP Setup:**
1. Set `SPI_TEST_MODE` to `2`
2. Build and load via JTAG
3. DSP will wait for RPi to drive SPI clock

**RPi Setup:**
1. Copy `rpi_spi_test.c` to RPi
2. Compile: `gcc -O2 -o spi_test rpi_spi_test.c -lm`

**Wiring (RPi → DSP LaunchPad):**
```
RPi MOSI (GPIO 10) → DSP GPIO 58 (SIMO)
RPi MISO (GPIO  9) → DSP GPIO 59 (SOMI)
RPi SCLK (GPIO 11) → DSP GPIO 60 (CLK)
RPi CE0  (GPIO  8) → DSP GPIO 61 (STE) + GPIO 123 (CS mirror)
RPi GND            → DSP GND
```

**Test sequence (ramp clock speed):**
```bash
# Start slow
sudo ./spi_test /dev/spidev0.0 1000000 1000    # 1 MHz, 1000 frames

# Medium speed
sudo ./spi_test /dev/spidev0.0 5000000 1000    # 5 MHz

# Target speed
sudo ./spi_test /dev/spidev0.0 10000000 1000   # 10 MHz
```

**RPi output shows:**
- Frame rate (Hz)
- Valid frame percentage
- CRC errors
- Throughput (KB/s)

**DSP watch variables:**
- `g_stats.rx_frame_count` should match RPi frame count
- `g_stats.crc_errors` should be 0

**Pass criteria:** >99.9% valid frames at 10MHz over 10,000 frames.

**Troubleshooting:**
- If CRC errors at high speed: check wiring, try shorter cables
- If no data received: verify SPI mode (RPi Mode 1 ↔ DSP Mode 3)
- If intermittent: check CS mirror wiring, add pullup on CS
- Try Mode 0 on DSP (`SPI_PROT_POL0PHA0`) + Mode 0 on RPi if Mode 3/1 combo fails

---

## Phase 2: Project Portability & Build System — COMPLETE ✓

| Task | Status | Notes |
|------|--------|-------|
| `.project`/`.cproject` use `${COM_TI_C2000WARE_INSTALL_DIR}` | DONE | No absolute paths — already portable |
| Fix RAM linker `.text` overflow | DONE | Using RAMGS10-12 on PAGE 0 |
| Create FLASH linker `.cmd` | DONE | `2837xD_FLASH_lnk_cpu1.cmd` with LOAD/RUN for ramfuncs |
| Add `.gitignore` for CCS artifacts | DONE | Ignores build folders, .out, .map, .obj etc. |
| C2000Ware version + CCS setup | DONE | See below |

### Setup Requirements

- **CCS version**: 12.x or later
- **C2000Ware version**: 6.0.1.00 (set `COM_TI_C2000WARE_INSTALL_DIR` in CCS workspace preferences)
- **Compiler**: TI C2000 CGT 22.6.2.LTS
- **Build config to use**: `CPU1_RAM_NON_VCU_CRC` (software CRC, no VCU hardware dependency)

### Build configs

| Config | When to use |
|--------|-------------|
| `CPU1_RAM_NON_VCU_CRC` | Development / JTAG debug (loads to RAM, fast iteration) |
| `CPU1_RAM_VCU_CRC` | Same but with hardware VCU CRC (not used — spi_dma.c implements CRC in SW) |
| `CPU1_FLASH_NON_VCU_CRC` | Production flash image (create by cloning RAM config, swap linker cmd) |

### FLASH linker notes (`2837xD_FLASH_lnk_cpu1.cmd`)

After linking with the FLASH config, call this in `main()` before enabling interrupts:
```c
extern uint16_t RamfuncsLoadStart, RamfuncsLoadSize, RamfuncsRunStart;
memcpy(&RamfuncsRunStart, &RamfuncsLoadStart, (size_t)&RamfuncsLoadSize);
```
(Or call `Device_init()` which handles ramfunc copy automatically.)

---

## Phase 3: CLA PID Control — COMPLETE ✓

| Task | Status | Notes |
|------|--------|-------|
| 6-axis discrete PID with back-calculation anti-windup | DONE | `cla_pid.cla` |
| CpuToClaMsg_t / ClaToCpuMsg_t / ClaGains_t structs | DONE | `cla_pid.h` |
| CLA memory config + CPU Timer 0 trigger | DONE | `cla_setup.c` |
| Gains updated via `CLA_setGains()` from CPU | DONE | Pre-scaled ki/kd |
| CLA trigger via CPU Timer 0 at configurable rate | DONE | Default 1 kHz |

### Files Added

| File | Description |
|------|-------------|
| `cla_pid.h` | Shared struct definitions |
| `cla_pid.cla` | CLA Task 1 (6-axis PID) + Tasks 2-8 stubs |
| `cla_setup.h` | CLA init API |
| `cla_setup.c` | MemCfg, timer, program copy, EOT ISR |

### Linker changes (both RAM + FLASH)
- `2837xD_RAM_lnk_cpu1.cmd`: removed RAMLS4 from `.text`, updated CLA sections to use hardware message RAM addresses (0x001480 / 0x001500), added Cla1Prog LOAD_START/SIZE/RUN_START
- `2837xD_FLASH_lnk_cpu1.cmd`: same hardware message RAM addresses, fixed `_Cla1funcsLoadStart` → `Cla1funcsLoadStart` (EABI, no underscore)

### Design choices
- **CpuToClaMsg_t** (CPUTOCLA1MSGRAM, 0x001480): ref[6] + pos[6] = 24 words
- **ClaToCpuMsg_t** (CLA1TOCPUMSGRAM, 0x001500): u[6] + err_flags + cycle_count = 15 words
- **ClaGains_t** (Cla1DataRam0 / RAMLS5, CPU+CLA1): kp/ki_dt/kd_inv_dt/u_max each [6] = 48 words
- **Anti-windup**: back-calculation — `integ += ki_dt*e + (u_sat - u_raw)` on each tick
- **Gains pre-scaled**: `ki_dt = ki*dt`, `kd_inv_dt = kd/dt` avoids runtime division in CLA

### Phase 3 Test Instructions

#### Test 3A: CLA heartbeat (no motors, no SPI needed)

**Requires:** RAM build loaded via JTAG.

1. Build `CPU1_RAM_NON_VCU_CRC`, load via JTAG.
2. Start execution.
3. Open CCS watch window, add watch expressions:
   - `gClaToCpu.cycle_count` — should increment at ~1000/s
   - `gClaToCpu.u` — should be float array, all zeros (ref=0, pos=0)
   - `gClaToCpu.err_flags` — should be 0x00
   - `gClaGains.kp` — should be [0.1, 0.1, ...] (default)
4. Wait 5 seconds. Verify `cycle_count` has increased by ~5000.

**Pass:** `cycle_count` increments at 1 kHz, all outputs zero, no errors.

#### Test 3B: Step response (manual setpoint via CCS)

1. With debug running, pause execution.
2. In Memory Browser, write `gCpuToCla.ref[0] = 1000.0f` (setpoint = 1000 counts).
3. Set `gCpuToCla.pos[0] = 0`.
4. Resume execution.
5. Observe `gClaToCpu.u[0]` — should show kp * 1000 = 100.0, clamped to u_max = 1.0.
6. Manually write increasing `gCpuToCla.pos[0]` values toward 1000.
7. Observe `gClaToCpu.u[0]` decrease toward 0 as error decreases.

**Pass:** u[0] tracks error proportionally, saturates correctly at ±1.0.

---

## Phase 4: Encoder Extensions — COMPLETE ✓

| Task | Status | Notes |
|------|--------|-------|
| EQEP1/2/3 on CPU1 (3 axes) | DONE | `encoders.c` |
| IPC shared RAM read for axes 3-5 | DONE | `gCpu2EncoderData` |
| Qualification helpers | DONE | `Encoders_applyQualificationEQEPx()` |

### Files Added

| File | Description |
|------|-------------|
| `encoders.h` | 6-axis API, IPC struct |
| `encoders.c` | EQEP1/2/3 impl + CPU2 IPC read |

### Axis mapping

| Axis | Source | GPIO pins | Notes |
|------|--------|-----------|-------|
| 0 | EQEP1 | GPIO20/21/23 | High-speed hardware decoder |
| 1 | EQEP2 | GPIO54/55/57 | High-speed hardware decoder |
| 2 | EQEP3 | GPIO28/29/31 | **Verify against PCB** |
| 3-5 | CPU2 IPC | CPU2 GPIO | Requires CPU2 project |

**Note:** For axes 3-5, a companion CPU2 project must write encoder counts to
CPU2TOCPU1RAM via the `Cpu2EncoderData_t` struct layout. If CPU2 is not running,
axes 3-5 report 0. See IPC struct in `encoders.h`.

### Phase 4 Test Instructions

#### Test 4A: Static encoder read

1. Build + load RAM config via JTAG.
2. Watch: `gEncPos[0]`, `gEncPos[1]`, `gEncPos[2]`, `gEncErrors`.
3. All should read 0 at start.
4. Manually turn EQEP1 encoder shaft by hand.
5. Observe `gEncPos[0]` changing (should track rotational position in counts).

**Pass:** gEncPos[0] tracks encoder rotation; no spurious errors.

#### Test 4B: Bi-directional count

1. Turn encoder forward → `gEncPos[0]` increases.
2. Turn encoder backward → `gEncPos[0]` decreases.
3. Call `Encoders_zeroPosition()` (breakpoint or write via CCS) → all positions reset to 0.

**Pass:** Sign of count matches rotation direction, zero resets correctly.

---

## Phase 5: Integration — COMPLETE ✓

| Task | Status | Notes |
|------|--------|-------|
| Combine DMA SPI + CLA PID + encoders + ADC + motors | DONE | `main.c` |
| 6-axis H-bridge motor driver | DONE | `motor.c/h` |
| Full TX telemetry frame | DONE | pos, u, ref, adc, err |
| Command timeout safety | DONE | 50 ms timeout → zero refs |
| LED heartbeat | DONE | LED1 blinks at 2 Hz via CLA count |

### Files Added

| File | Description |
|------|-------------|
| `main.c` | Integration: system init, callbacks, main loop |
| `motor.h` | 6-axis H-bridge API |
| `motor.c` | EPWM1-6 + direction GPIO control |

### Motor GPIO assignment (update to match PCB)

| Axis | PWM (EPWM output) | DIR1 GPIO | DIR2 GPIO |
|------|-------------------|-----------|-----------|
| 0 | EPWM1A (GPIO0)  | GPIO24 | GPIO25 |
| 1 | EPWM2A (GPIO2)  | GPIO26 | GPIO27 |
| 2 | EPWM3A (GPIO4)  | GPIO32 | GPIO33 |
| 3 | EPWM4A (GPIO6)  | GPIO34 | GPIO35 |
| 4 | EPWM5A (GPIO8)  | GPIO36 | GPIO37 |
| 5 | EPWM6A (GPIO10) | GPIO38 | GPIO39 |

Edit `motor.c` `kPwmPinCfg[]`, `kDir1Gpio[]`, `kDir2Gpio[]` arrays to match your PCB.

### Phase 5 Test Instructions

#### Test 5A: Full integration (no motors connected — safe test)

1. Do NOT connect motors for this test.
2. Build `CPU1_RAM_NON_VCU_CRC`, load via JTAG.
3. Run RPi SPI test: `sudo ./spi_test /dev/spidev0.0 1000000 1000`
4. Watch in CCS:
   - `gClaToCpu.cycle_count` — increments at 1 kHz
   - `gEncPos` — all zeros (no encoder connected)
   - `gAdc` — raw ADC counts from ADCA-D channels
   - `g_stats.rx_frame_count` — matches RPi frame count
   - `g_stats.crc_errors` — should be 0
5. Watch RPi output: Valid frame % should be 100%.

**Pass:** SPI frames received, CLA running, ADC sampling, no errors.

#### Test 5B: Motor output test (motors connected, bench only)

⚠️ **SAFETY: Use a bench power supply with current limiting. Start with no load.**

1. Connect a single motor to axis 0 H-bridge.
2. In CCS, set `gCpuToCla.ref[0] = 100.0f` (small setpoint).
3. Observe: motor moves briefly, u[0] converges to zero as pos tracks ref.
4. Verify direction reversal: set `gCpuToCla.ref[0] = -100.0f`.
5. Verify stop: set `gCpuToCla.ref[0] = 0.0f` → motor brakes.

**Pass:** Motor responds to reference, direction correct, stops on zero ref.

#### Test 5C: Command timeout

1. Start RPi SPI test.
2. Stop RPi program after a few seconds.
3. Wait >50 ms.
4. Observe `gCpuToCla.ref` — should all go to 0.0 within 50 ms.
5. Motor (if connected) should stop.

**Pass:** Robot safe-stops within 50 ms of losing RPi communication.

#### Test 5D: Full speed at 10 MHz

```bash
sudo ./spi_test /dev/spidev0.0 10000000 10000
```

Watch CCS: crc_errors = 0, rx_frame_count = 10000.
Watch RPi: valid% = 100%.

**Pass:** Zero errors at 10 MHz over 10,000 frames.

---

## Reference Files

### Old project (working reference):
- `ccs/SPI_Slave_Motor_Control/MessageCenter.c` — SPI framing, CS ISR, double-buffered TX
- `ccs/SPI_Slave_Motor_Control/MessageCenter.h` — Frame structures, CRC
- `ccs/SPI_Slave_Motor_Control/main.c` — System init, CLA integration
- `ccs/SPI_Slave_Motor_Control/control.cla` — CLA task
- `ccs/SPI_Slave_Motor_Control/motor.c` — H-bridge PWM
- `ccs/SPI_Slave_Motor_Control/encoders.h` — EQEP interface
- `ccs/SPI_Slave_Motor_Control/util.h` — CpuToClaMsg/ClaToCpuMsg, pack/unpack float

### Key driverlib APIs:
```c
// DMA
DMA_initController(), DMA_configAddresses(), DMA_configBurst()
DMA_configTransfer(), DMA_configWrap(), DMA_configMode()
DMA_startChannel(), DMA_stopChannel(), DMA_enableTrigger()
DMA_getTransferStatusFlag(), DMA_clearTransferStatusFlag()

// SPI
SPI_setConfig(), SPI_enableFIFO(), SPI_enableLoopback()
SPI_setFIFOInterruptLevel(), SPI_enableTalk()
SPI_resetTxFIFO(), SPI_resetRxFIFO()

// GPIO / Interrupt
GPIO_setInterruptPin(), GPIO_setInterruptType()
GPIO_enableInterrupt(), Interrupt_register(), Interrupt_enable()
```
