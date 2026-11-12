# Project Crazy Diamond — current system

This directory is the authoritative description of the current PCD host system.
Update the relevant page in the same change as its implementation. Historical
proposals and acceptance reports are evidence, not current interface definitions.

| Read | Covers |
|---|---|
| [Architecture](architecture.md) | DSP, RT worker, ROS, control and sampling boundaries |
| [Controllers](controllers.md) | PID, optional internal reference limiter, profiles and tuning |
| [ROS API](ros_api.md) | Public messages, services, state and example calls |
| [Data](data.md) | Signal catalog, recorder, CSV library and runner semantics |
| [Robot packages](robot_packages.md) | Embedded-panel contract and phantom experiments |
| [Operations](operations.md) | Build, Git deployment, services, configuration and recovery |
| [Verification](verification.md) | Acceptance tests and measured limitations |
| [Full-duty stall investigation](duty_stall.md) | Captured commands, arm/fault states and physical signal checks |
| [DSP hardware guide](../../dsp/f28379d/README.md) | CPU responsibilities, pins, CCS build/upload/debug |
| [Historical documents](history/README.md) | Previous plans and test evidence |

The DSP firmware is unchanged by the experiment-tools pass. Current host code
runs feedback control and program execution at 2 kHz; the DSP is an I/O endpoint.
