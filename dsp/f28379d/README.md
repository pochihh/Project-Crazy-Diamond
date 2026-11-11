# F28379D DSP firmware

Dual-core firmware for the F28379D controlCARD on a TMDSHSECDOCK. The DSP is
PCD's motor/encoder/I/O endpoint; feedback controllers run on the Raspberry Pi.
The current firmware uses **protocol v5** and requires the matching v5 host.

## CPU responsibilities

| Component | Function |
|---|---|
| CPU1 | Initializes hardware and CPU2; handles SPI DMA; reads axes 0–2 through eQEP; drives all six motors, four DO and two DACs; reads four DI and six ADC inputs; assembles telemetry and enforces protection at **5 kHz** |
| CPU2 | Continuously polls three A/B pairs for axes 3–5 with x4 quadrature decoding; counts invalid transitions; publishes coherent snapshots at **5 kHz** |
| Pi | Runs the **2 kHz** feedback/SPI loop, calibration, reference generation, ROS 2, web UI and recording |

Motor PWM is **20 kHz**. SPI uses **10 MHz, 16-bit words, Linux mode 1**, with
96 words / 192 bytes per full-duplex frame. Telemetry combines the newest values
under one frame timestamp; acquisition is not simultaneous. CPU2 publishes
through double-buffered message RAM with sequence checks. CPU1 invalidates its
channels after 10 ms without a fresh snapshot. CLA controller files are retained
but excluded from active builds.

Boot/disarm/fault clears motor duty, STBY/shared EN, DO and DAC commands. A
**50 ms** active-command timeout latches a fault. The host synchronizes a session,
clears faults explicitly and arms outputs; motor mask zero permits I/O-only use.
Automatic homing, auxiliary encoders, current sensing and limit-switch handling
are not implemented. UI **Reset zero** changes the host offset, not DSP counts.

## Active pin map

Numbers are **HSEC dock pins**, not GPIO numbers. Software axes 0–5 correspond
to external motor channels 1–6. Digital signals are 3.3 V; encoder receiver
outputs require the existing 5 V-to-3.3 V translator.

### Motors — CPU1

Each cell is **dock pin / GPIO**. PWM uses ePWM1A–6A respectively. STBY high
permits drive. Shared `MOTOR_EN` is **71 / GPIO18**, high to enable.

| Axis | PWM | STBY | DIR |
|---:|---|---|---|
| 0 | 49 / 0 | 51 / 1 | 58 / 12 |
| 1 | 53 / 2 | 55 / 3 | 60 / 13 |
| 2 | 50 / 4 | 52 / 5 | 62 / 14 |
| 3 | 54 / 6 | 56 / 7 | 64 / 15 |
| 4 | 57 / 8 | 59 / 9 | 67 / 16 |
| 5 | 61 / 10 | 63 / 11 | 69 / 17 |

### Encoders

Each cell is **dock pin / GPIO**. eQEP index pins are configured, but counter
reset on index is disabled.

| Axis | Acquisition | A | B | Index |
|---:|---|---|---|---|
| 0 | CPU1 eQEP1 | 68 / 20 | 70 / 21 | 74 / 23 |
| 1 | CPU1 eQEP2 | 100 / 54 | 102 / 55 | 106 / 57 |
| 2 | CPU1 eQEP3 | 127 / 62 | 128 / 63 | 130 / 65 |
| 3 | CPU2 GPIO polling | 75 / 24 | 77 / 25 | — |
| 4 | CPU2 GPIO polling | 79 / 26 | 81 / 27 | — |
| 5 | CPU2 GPIO polling | 80 / 30 | 88 / 39 | — |

### SPI and general I/O — CPU1

| Signal | Dock pin(s) | GPIO / peripheral | Connection or range |
|---|---|---|---|
| MOSI | 108 | GPIO58 / SPIA SIMO | Pi physical 19 / BCM10 |
| MISO | 110 | GPIO59 / SPIA SOMI | Pi physical 21 / BCM9 |
| SCLK | 125 | GPIO60 / SPIA CLK | Pi physical 23 / BCM11 |
| CE0 / STE | 126 | GPIO61 / SPIA STE | Pi physical 24 / BCM8 |
| CE0 mirror | 89 | GPIO40 / XINT5 | Branch the **same CE0** here too |
| DI0–3 | 85, 87, 147, 148 | GPIO32, 33, 80, 81 | Inputs with pull-ups |
| DO0–3 | 121, 122, 123, 124 | GPIO35, 36, 37, 38 | Low at boot/stop/fault |
| AI0–5 | 12, 14, 18, 20, 24, 26 | ADCB0–5 | 12-bit single-ended, raw 0–4095 |
| AO0–1 | 9, 11 | DACA, DACB / ADCA0, ADCA1 | 12-bit outputs, raw 0–4095 |
| Logic ground | 135 or 157 | GND | Common Pi/DSP/translator/driver logic ground |

DACs use ADCA VREFHI; ADCB uses its own reference. Both controlCARD SW2 reference
switches were reported right / nominal **3.0 V** on 2026-11-11; absolute voltage
is unmeasured, so the UI reports raw codes. Keep analog inputs within 0–Vref.
Pins 9/11 are **DAC outputs**, replacing old current-input reservations.
Dock pin 122 is GPIO36, not a power supply.

See the [full wiring reference](../../PLAN/wiring_table.md) for receiver wiring,
reserved pins and test jumpers. Remove encoder-test jumpers before connecting
actual encoders; otherwise DO changes also drive those encoder inputs.

## Build and upload

Tools: **CCS 20.x**, **TI C2000 CGT 22.6.2.LTS**, **C2000Ware 6.00.01.00**, and
the on-card **XDS100v2** probe. Run laptop commands from the repository root.
Both cores must use the same build configuration and protocol sources.

1. Use **Stop All** in the web UI, then run on the Pi:

   ```sh
   sudo systemctl stop diamond-bridge
   ```

2. Connect the controlCARD debug USB. With board power off, set **A:SW1 position
   1 ON/up** for JTAG, then power on. This is separate from main boot switch
   SW1. Close any existing CCS debug session before using the loader.
3. Build and load **RAM first** for development:

   ```sh
   python3 scripts/build_dsp.py RAM
   /Applications/ti/ccs2020/ccs/scripting/run.sh scripts/load_dsp.js RAM
   ```

   The loader connects both cores, loads/verifies **CPU1 first, CPU2 last**, runs
   both, prints diagnostics, then disconnects. CPU1 load/reset can reset CPU2;
   reversing the order can leave CPU2 stopped or without its RAM program.
4. After RAM testing, preserve the working images and build/program **FLASH**:

   ```sh
   python3 scripts/build_dsp.py FLASH
   /Applications/ti/ccs2020/ccs/scripting/run.sh scripts/load_dsp.js FLASH
   ```

   Loading a FLASH `.out` programs that core's flash bank. Both cores remain
   halted while programming; the loader verifies both images before running.
5. Verify standalone boot: power off, set **A:SW1-1 OFF/down** and **both main
   SW1 positions OFF/up** (1,1 / Get Mode, flash by default), then power on.
   CPU1 starts CPU2 from FLASH. On the Pi:

   ```sh
   sudo systemctl start diamond-bridge
   ```

   Check v5 telemetry, no fault and encoder validity **0x3F** before enabling
   outputs. The bridge starts disarmed. A JTAG FLASH load alone does not prove
   standalone startup.

Outputs: `dsp/f28379d/cpu{1,2}/{RAM,FLASH}/f28379d_cpu{1,2}.out` and `.map`.
The build script compiles both cores with `-O2 -g`. Override `TI_CCS_ROOT`,
`TI_CGT_ROOT` or `C2000WARE_ROOT` for other installations; defaults are
`/Applications/ti/ccs2020/ccs`, its `tools/compiler/ti-cgt-c2000_22.6.2.LTS`, and
`~/ti/C2000Ware_6_00_01_00`. DriverLib's
`driverlib/f2837xd/driverlib/ccs/Debug/driverlib.lib` must exist. The loader
requires CCS's `scripting/run.sh`, not plain Node.js.

## Debug in CCS

Import the existing `dsp/f28379d/cpu1` and `cpu2` projects without copying them
out of the repository: relative links use sibling `device/`, `shared/` and
`dsp/common/`. Select **RAM** or **FLASH** explicitly for each project. Set CGT
to 22.6.2.LTS and resolve `COM_TI_C2000WARE_INSTALL_DIR` to C2000Ware 6.00.01.00.
Build both projects, or debug the `.out` files produced by the script above.

With the Pi bridge stopped and outputs disabled:

1. In **Target Configurations**, import/open
   `dsp/f28379d/targetConfigs/TMS320F28379D.ccxml` and choose
   **Start Project-less Debug**. It selects XDS100v2 and TMS320F28379D.
2. In **Threads**, connect CPU1, perform a System Reset, then connect CPU2.
   Keep both halted; disable automatic run-to-main while loading if needed.
3. Select CPU1, then **Run → Load → Load Program** with its `.out`; load CPU2
   **last**. Verify both programs, then resume both cores. Select the relevant
   core in Threads before inspecting its variables or memory.
4. Use breakpoints/stepping for initialization and disabled-output inspection.
   Resume both cores and remove breakpoints before restarting the Pi bridge.

See [TI's CCS debug guide](https://software-dl.ti.com/ccs/esd/documents/users_guide_ccs_20.1.1/ccs_debug-main.html)
for menu details. Default TI GEL initialization **resets the target and clears
RAM on connect**. Loading symbols alone does not prevent that connection-time
reset. Use SPI/UI telemetry for observation that must not interrupt operation.

**Halting is not a motor stop:** PWM/eQEP peripherals free-run, and CPU1's
software watchdog cannot execute while CPU1 is halted. Stop outputs before
debugging. Halting CPU2 makes its encoder data stale to CPU1.

| Core | Useful watch expressions | Expected / meaning |
|---|---|---|
| CPU1 | `gTickCount`, `gMaxTickUs` | 5 kHz tick; maximum handler duration in µs, below 200 µs |
| CPU1 | `gEncoderValid`, `gEncPos`, `gEncErrors` | `0x3F`: all acquisition paths live; `0x07`: CPU2 unavailable |
| CPU1 | `gCpu2MaxPollUs`, `endpoint.fault` | Polling gap; fault bits 1=timeout, 2=command, 4=encoder unavailable |
| CPU2 | `gPolls`, `gMaxPollCycles` | Advancing poll count; maximum whole-loop gap in 200 MHz CPU cycles |

Optimization may hide locals or change stepping order. Keep `-O2` for timing
measurements and inspect the diagnostic globals instead.

| Symptom | First check |
|---|---|
| ICEPick −2131 / scan path stuck | Board power, debug USB and **A:SW1-1 ON/up** |
| Encoder validity stays `0x07` | CPU2 image loaded **after CPU1**, and CPU2 running |
| Invalid SPI telemetry | Matching v5 host; CE0 wired to **126 and 89**; only one SPI client |
| Timeout after a breakpoint | Reload/run both as needed, restart bridge and explicitly clear fault |
| Runs only under debugger | Both FLASH images programmed, main SW1 Get Mode, CPU2 startup validity |

## Source map and verification

| Path | Purpose |
|---|---|
| `cpu1/main.c`, `spi_dma.c` | Startup, endpoint tick, SPI DMA/framing |
| `cpu1/motor.c`, `encoders.c`, `io.c` | PWM/DIR/STBY, eQEP/IPC snapshots, DIO/ADC/DAC |
| `cpu2/main.c`, `shared/ipc_layout.h` | Quadrature polling and inter-core data layout |
| `../common/{protocol,endpoint,quadrature}.h` | Wire codec, endpoint state machine, decoder |
| `device/`, `cpu*/2837xD_*_lnk_cpu*.cmd` | Device startup and RAM/FLASH memory layout |
| `../host/spi.hpp` | Matching Pi transport; `legacy_spi.hpp` is for old firmware |
| `../../scripts/{build_dsp.py,load_dsp.js}` | Build and load/verify helpers |

RAM/FLASH two-axis regressions, standalone boot, four digital loopbacks, both
DACs into all six ADC inputs, and three CPU2 functional encoder tests passed.
Full-speed CPU2 encoders, other physical motor channels and absolute analog
calibration remain outside that coverage. See [test evidence](../../PLAN/STAGE3_RESULTS.md),
[protocol details](../../PLAN/spi_protocol.md) and
[host operation/tests](../../PLAN/STAGE2_OPERATION.md).
