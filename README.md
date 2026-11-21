# Project Crazy Diamond

Crazy Diamond runs a 2 kHz motion controller on a Raspberry Pi and uses a dual-core TI F28379D as its motor, encoder and I/O endpoint. Heart Phantom uses three linear axes. Operators can command them from the browser, from ROS 2, or by playing a CSV with one row per control tick.

Start with the **[system guide](doc/README.md)**. It links to bench operation, controller tuning, the ROS interfaces, DSP wiring and firmware builds.

The bench control desk is at `http://192.168.1.21:8000/` on the LAN. Closing the browser leaves the bridge in control. Use **Stop All** to disable the outputs.
