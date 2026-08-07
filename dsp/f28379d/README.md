# F28379D firmware

The dual-core DSP uses two CCS executable projects because CPU1 and CPU2 have
independent program images, linker maps, and debug targets. There is no third
CCS project.

```text
f28379d/
├── cpu1/          CCS project: control, CLA, motor I/O, SPI DMA, homing
├── cpu2/          CCS project: encoder edge ISRs and 5 kHz IPC publication
├── shared/        headers shared by both cores; not a library
├── device/        common TI device support/startup files
└── targetConfigs/ shared dual-core debug target
```

Import `cpu1` and `cpu2` as existing CCS projects. CPU1 has `RAM` and `FLASH`
build configurations; CPU2 remains RAM-only. A dual-core debug session must
load both images. CPU1 owns system initialization and peripheral assignment
before CPU2 encoder interrupts are enabled.

CPU2 is intentionally safe-idle until the encoder ISR/rate-test milestone is
implemented. CPU1's SPI, eQEP, and PWM paths have passed the initial hardware
bring-up. It boots with every motor disabled and requires valid host ARM and
DUTY commands; the command watchdog disables the outputs after 50 ms without
a valid update.

## CPU1 flash build

In CCS, right-click `f28379d_cpu1`, then select **Build Configurations > Set
Active > FLASH** and build it. Load `cpu1/FLASH/f28379d_cpu1.out` into the CPU1
target; CCS erases and programs the required flash sectors automatically.

For programming through the controlCARD's onboard XDS100v2, set `A:SW1`
position 1 up (`ON`). For standalone boot, power off, set `A:SW1` position 1
down (`OFF`), set both positions of the main `SW1` up (`1,1`: Get Mode, Flash
by default), then power-cycle the board. The Raspberry Pi must still send the
normal ARM and DUTY commands before a motor can move.

C2000Ware is referenced through CCS product variables. The repository does not
vendor a second copy of DriverLib.
