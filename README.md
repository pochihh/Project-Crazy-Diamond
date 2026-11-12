# Project Crazy Diamond

PCD is a reusable realtime axis/I/O system built from a Raspberry Pi and a
TMS320F28379D DSP. The Pi runs the controllers, tick-synchronous program runner,
recorder and ROS API; the DSP applies duty/I/O and reports encoder/analog feedback.
Two calibrated rack-and-pinion axes are commissioned on the bench.

**Start with the [current system guide](ros2_ws/doc/README.md).** It is the
maintained source of truth. `PLAN/` links to historical material and retains the
original UI references; old proposals are not current interface specifications.

- [Architecture and process boundaries](ros2_ws/doc/architecture.md)
- [Controllers, profiles and optional reference limiting](ros2_ws/doc/controllers.md)
- [ROS commands and state](ros2_ws/doc/ros_api.md)
- [Data Logger, Data Runner and CSV format](ros2_ws/doc/data.md)
- [External robot panels and Heart Phantom](ros2_ws/doc/robot_packages.md)
- [Build, services, Git deployment and recovery](ros2_ws/doc/operations.md)
- [Verification and limits](ros2_ws/doc/verification.md)
- [DSP CPUs, pin table, build/upload and CCS debugging](dsp/f28379d/README.md)

The web desk runs at `http://192.168.1.21:8000/` on the bench LAN. It has direct-axis
controls, Logger, Runner, System and Resources tabs. A robot package supplies the
optional top-of-dashboard panel; no robot node means no robot panel. Stop All
remains visible throughout.

Outputs start disabled. Manual zero is **disable → move → Reset zero**. Browser
closure does not stop control, recording or playback. Use explicit Stop when
ending an experiment. The Pi and laptop use the same Git repository; runtime
recordings and uploaded CSVs are stored separately from tracked source.
