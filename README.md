# Project Crazy Diamond

Real-time motor control and data acquisition using a Raspberry Pi and a
TMS320F28379D DSP. Two axes have run stably from CSV input on the bench.
ROS 2 Jazzy and the web control desk now run both axes in calibrated millimeters
using DSP protocol v5. The firmware migration passed two-axis RAM/FLASH and
standalone-boot tests, dock DIO/AIO loopbacks, and CPU2 functional encoder tests.
See [stage-3 results and coverage limits](PLAN/STAGE3_RESULTS.md).

## Web control desk

Open **http://192.168.1.21:8000/** on the local network. The Pi services start
disarmed. Enable the system and each desired motor, then drag its position slider
within ±50 mm. Releasing the slider or closing the browser keeps the held target.

To establish a manual zero: **disable the motor → move it manually → Reset zero**.
Zero otherwise comes from the position when the host controller starts.

Use Start recording and Stop & save to download CSV. Stop all disables every motor and general I/O output.
The **Heart Phantom** dashboard has position/PWM control, live plots, duty meters,
a fixed Stop All header, and an icon sidebar. System shows Pi usage and expandable
10 Hz ROS topic views. Browser settings control theme, display rates, and performance
stats. PWM holds duty on release/disconnect; switch modes with the axis disabled.

Each axis opens **Controller settings** for live PID tuning and reusable YAML
profiles. Save affects the named profile; other axes require explicit reload.
See [project profiles and controller development](projects/README.md).

- [Build, deployment, mock mode, and operation](PLAN/STAGE2_OPERATION.md)
- [Bench acceptance results and remaining limits](PLAN/STAGE2_RESULTS.md)

## Documentation

- [ROS bridge architecture, topics, services and DSP/UI data flow](ros2_ws/README.md)

- [Git synchronization and Pi deployment](PLAN/GIT_WORKFLOW.md)
- [Current system and working SPI contract](PLAN/CURRENT_SYSTEM.md)
- [Real-time fault logs and stability checks](PLAN/REALTIME_STABILITY.md)
- [Four-stage implementation roadmap](PLAN/IMPLEMENTATION_PLAN.md)
- [System design and open decisions](PLAN/ROS2_plan.md)
- [Complete documentation guide](PLAN/plan.md)

The commands below describe the **legacy v2/v3 firmware** sine/probe tool.
They are incompatible with v5; use the ROS bridge for the current firmware. The working CSV
implementation reported from the bench has not yet been located in this checkout.
Stop `diamond-bridge.service` before running a bench tool: only one process may
own the physical SPI device. Restart the service when finished; it starts disarmed.

## Raspberry Pi bench commands

Run these commands on the Raspberry Pi from the repository clone:

```bash
cd ~/Project-Crazy-Diamond
```

## Build the motor test

```bash
./build_test.sh
```

The binary stays at `build/rpi_motor_control_test`; installation is not
required. Re-run `./build_test.sh` after changing the user-tuning block in
`dsp/f28379d/cpu1/tests/rpi_motor_control_test.cpp`.

## Run the continuous two-axis sine

The default run uses a 1 kHz Pi-side PI controller and continues until
`Ctrl-C`:

```bash
./run_test.sh
```

Current source defaults:

| Setting | Axis 0 | Axis 1 |
|---|---:|---:|
| `Kp` | 0.0005 | 0.0005 |
| `Ki` | 0.001 | 0.001 |
| `Kd` | 0 | 0 |
| Amplitude | 2,000 counts | 2,000 counts |
| Motor sign | -1 | -1 |
| Maximum duty | 100% | 100% |

The shared sine period is 1 second. Duration `0` means continuous. Edit the
`User tuning` block in
`dsp/f28379d/cpu1/tests/rpi_motor_control_test.cpp` to tune these values, then
rebuild the binary.

For a bounded run, add a duration in seconds:

```bash
./run_test.sh --sine --duration 3
```

Command-line values can temporarily override the source defaults, for example:

```bash
./run_test.sh --sine --kp0 0.0006 --amp0 1500 --period 2
```

Set one amplitude to zero to disable that axis for an isolated test:

```bash
./run_test.sh --sine --amp1 0 --period 5
```

## Probe H-bridge axes

Use a short, unloaded pulse before running a newly wired axis:

```bash
./run_test.sh --probe-axis 0 --probe-duty 0.03 --probe-ms 250
```

Use negative duty to verify the other direction. Change `--probe-axis` to `1`
for axis 1. Repeat `--probe-axis` to drive multiple axes, and use `--probe-ms 0`
to hold until `Ctrl-C`:

```bash
./run_test.sh --probe-axis 0 --probe-axis 2 --probe-duty 0.1 --probe-ms 0
```

## Wired bench LAN

The Pi Ethernet interface uses static address `192.168.2.10/24` with no
gateway. Configure the connected computer as another address in the same
subnet, such as `192.168.2.11/24`, then connect with:

```bash
ssh toby@192.168.2.10
```

The Ethernet configuration activates automatically after boot whenever a
cable is connected. Wi-Fi remains available separately during development.

## DSP FLASH boot and debugger switches

`A:SW1` and the main `SW1` are different switches on the F28379D controlCARD:

| Switch | Function | CCS programming/debug | Standalone FLASH boot |
|---|---|---|---|
| `A:SW1` position 1 | Connects the onboard XDS100v2 debugger | `ON` / up | `OFF` / down |
| Main `SW1` position 1 | Boot-mode GPIO72 | May remain `OFF` / up | `OFF` / up (`1`) |
| Main `SW1` position 2 | Boot-mode GPIO84 | May remain `OFF` / up | `OFF` / up (`1`) |

After programming the FLASH image, verify standalone boot as follows:

1. Stop the Pi motor test and leave motor power off or the E-stop engaged.
2. Terminate or disconnect the CCS debug session.
3. Power off the controlCARD. Never change these switches while powered.
4. Set `A:SW1` position 1 to `OFF` / down. Leave its position 2 unchanged.
5. Set both positions of the main `SW1` to `OFF` / up (`1,1`).
6. Disconnect the debugger USB if desired, then power the board on.

The DSP now boots from FLASH without CCS. Firmware starts with every motor
disarmed; the Pi must send valid ARM and DUTY commands before motion is
possible.

To program or debug again, power off, set `A:SW1` position 1 to `ON` / up,
connect the debugger USB, power on, and start the CCS session. The main `SW1`
can remain at `1,1` because JTAG loading does not depend on the boot switches.

## SPI communication-only test

Only use `--motor-power-off` when motor power is physically disconnected:

```bash
~/.local/bin/rpi_spi_test --motor-power-off \
  /dev/spidev0.0 10000000 30000 1 1000
```

For the 10-minute RT acceptance test:

```bash
sudo taskset -c 3 chrt -f 80 ~/.local/bin/rpi_spi_test --motor-power-off \
  /dev/spidev0.0 10000000 600000 1 1000
```

## Stopping and safety

- `Ctrl-C` requests a clean DSP disarm.
- The DSP disarms after 50 ms without valid duty commands.
- The program disarms on invalid/stale telemetry, a 1 kHz deadline miss, or
  excessive tracking error.
- Keep the physical E-stop ready during motor tests.
