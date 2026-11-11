# Project Crazy Diamond — Documentation Guide

Updated: 2026-11-11

Project Crazy Diamond is a real-time control and data-acquisition system for
control research. The Raspberry Pi runs the host controller and web application;
a TMS320F28379D provides encoder, motor, and general I/O access.

Two axes have run stably from CSV input, as reported by the project owner.
ROS 2 and browser position control operate those axes. Protocol v5 adds six
encoder channels and general I/O; [stage-3 results](STAGE3_RESULTS.md) record
completed RAM/FLASH bench acceptance, standalone boot, wired I/O tests, and
CPU2 functional counting, with the remaining coverage limits.

## Read in this order

| Document | Purpose |
|---|---|
| [Current system](CURRENT_SYSTEM.md) | Working baseline, source-backed behavior, current SPI contract, and limitations |
| [Stage-2 operation](STAGE2_OPERATION.md) | Web controls, zero/reset, recording, build, services, and mock mode |
| [Stage-3 results](STAGE3_RESULTS.md) | Firmware migration, timing, I/O/encoder test evidence and coverage limits |
| [Robot/controller design](ROBOT_AND_CONTROLLERS.md) | Implemented PID profile contract and proposed robot/LQR integration |
| [Project profiles](../projects/README.md) | Live tuning, YAML persistence, startup selection and compiled controller development |
| [Stage-2 results](STAGE2_RESULTS.md) | Automated and physical acceptance evidence and its limits |
| [Implementation roadmap](IMPLEMENTATION_PLAN.md) | Four agreed stages and completion criteria |
| [System design](ROS2_plan.md) | Accepted requirements, proposed architecture, and open decisions |
| [ROS 2 interfaces](ros2_interfaces.md) | Stage-2 responsibilities and stage-3 extensions |
| [SPI migration](spi_protocol.md) | Implemented v5 wire layout and state/recovery rules |
| [Electronics](electronics.md) | Existing board constraints and proposed expansion pin allocation |
| [Dock wiring](wiring_table.md) | Physical connector reference; not a completed wiring/test record |
| [Bench commands](../README.md) | Build, run, network, stop, and flash-boot procedures |
| [Firmware setup](../dsp/f28379d/README.md) | CCS project setup |
| [Development history](DSP_DEV_HISTORY.md) | Historical experiments, not the current contract |

## Document ownership

- `CURRENT_SYSTEM.md` describes observed/source-backed behavior.
- `ROS2_plan.md` owns requirements and unresolved decisions.
- `IMPLEMENTATION_PLAN.md` owns delivery order and acceptance.
- Interface documents supply technical detail under those requirements.
  Stage-3 proposals must not become prerequisites for stage 2.
- Selected hardware pin changes belong in both electronics and wiring documents.
  This documentation update does not change existing pin assignments.

The November roadmap replaces the August M0–M8 execution order. Older detailed
schemas and proposed layouts remain in Git history. Recommendations awaiting
review are explicitly distinguished from accepted requirements.
