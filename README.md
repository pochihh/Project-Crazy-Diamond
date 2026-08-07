# Project Crazy Diamond — Raspberry Pi Bench Commands

Run these commands on the Raspberry Pi from the repository clone:

```bash
cd ~/Project-Crazy-Diamond
```

## Update and build the motor test

```bash
git pull --ff-only

g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  dsp/f28379d/cpu1/tests/rpi_motor_control_test.cpp \
  -o /tmp/rpi_motor_control_test

install -m 0755 /tmp/rpi_motor_control_test ~/.local/bin/rpi_motor_control_test
~/.local/bin/rpi_motor_control_test --self-test
```

## Run the continuous two-axis sine

The default run uses a 1 kHz Pi-side PI controller and continues until
`Ctrl-C`:

```bash
sudo taskset -c 3 chrt -f 80 ~/.local/bin/rpi_motor_control_test \
  --enable-motors --sine
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
`Normal sine-run defaults` block in
`dsp/f28379d/cpu1/tests/rpi_motor_control_test.cpp` to tune these values, then
rebuild and reinstall the binary.

For a bounded run, add a duration in seconds:

```bash
sudo taskset -c 3 chrt -f 80 ~/.local/bin/rpi_motor_control_test \
  --enable-motors --sine --duration 3
```

Command-line values can temporarily override the source defaults, for example:

```bash
sudo taskset -c 3 chrt -f 80 ~/.local/bin/rpi_motor_control_test \
  --enable-motors --sine --kp0 0.0006 --amp0 1500 --period 2
```

## Probe H-bridge axes

Use a short, unloaded pulse before running a newly wired axis:

```bash
sudo taskset -c 3 chrt -f 80 ~/.local/bin/rpi_motor_control_test \
  --enable-motors --probe-axis 0 --probe-duty 0.03 --probe-ms 250
```

Use negative duty to verify the other direction. Change `--probe-axis` to `1`
for axis 1. Repeat `--probe-axis` to drive multiple axes, and use `--probe-ms 0`
to hold until `Ctrl-C`:

```bash
sudo taskset -c 3 chrt -f 80 ~/.local/bin/rpi_motor_control_test \
  --enable-motors --probe-axis 0 --probe-axis 2 --probe-duty 0.1 --probe-ms 0
```

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
