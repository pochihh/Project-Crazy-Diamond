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

Import `cpu1` and `cpu2` as existing CCS projects. Both currently have one
configuration named `RAM`; each produces its own `.out`. A dual-core debug
session must load both images. CPU1 owns system initialization and peripheral
assignment before CPU2 encoder interrupts are enabled.

CPU2 is intentionally safe-idle until the encoder ISR/rate-test milestone is
implemented. CPU1 contains the earlier integration prototype and is not yet an
energized-hardware release; follow the locked pin map in `PLAN/electronics.md`
as that code is brought forward.

C2000Ware is referenced through CCS product variables. The repository does not
vendor a second copy of DriverLib.
