# System guide

The Pi runs the controllers, CSV runner and recorder. The DSP applies motor duty and general outputs and returns raw feedback over SPI. Heart Phantom adds an optional experiment panel to the control desk.

| Task | Maintained reference |
|---|---|
| Follow a command from ROS or the browser to an output, and feedback back to a recording | [System architecture](architecture.md) |
| Build the Pi software, manage the four services, zero an axis or recover a fault | [Host operations](../ros2_ws/doc/operations.md) |
| Connect motors, encoders, the Pi or dock I/O, or build and debug either DSP core | [DSP hardware and CCS guide](../dsp/f28379d/README.md) |
| Decode a frame, counter or DSP state transition | [SPI v5 and endpoint state machine](../dsp/DSP_FINITE_STATE_MACHINE.md) |
| Publish commands or integrate another ROS client | [ROS interfaces](../ros2_ws/doc/ros_api.md) |
| Tune the PID, run Step Recorder or match controller signals to DSP feedback | [Controllers and profiles](../ros2_ws/doc/controllers.md) |
| Create a playback CSV, select signals or download a recording | [Runner and Logger](../ros2_ws/doc/data.md) |
| Run the two-axis Phantom experiment or embed another robot's panel | [Robot packages](../ros2_ws/doc/robot_packages.md) |
| Test a change or distinguish a transport fault from a driver or display problem | [Verification](verification.md) · [Troubleshooting](troubleshooting.md) |

## Heart Phantom bench

| Axis | Mechanism | Slider range from the bridge zero | Typical command source |
|---|---|---|---|
| 0 and 1 | Rack and pinion, CPU1 eQEP1 and eQEP2 | −50 to +50 mm | Manual controls, Phantom sine or saved two-axis program |
| 2 | Rack and pinion, CPU1 eQEP3 | 0 to 35 mm | Manual controls or a single-axis program in Data Runner |

The three axes use reversed coordinates. The bridge applies the sign to both motor commands and measured displacement. Slider bounds describe the UI. For the installed calibration and startup profile assignments see the [project configuration](../projects/heart_phantom/bridge.yaml).

The [axis 2 breathing program](../projects/heart_phantom/profiles/MID102_axis2_30mm_2kHz.csv) commands 0–30 mm over 35,796 ticks (17.898 s). Select it in **Data → Runner** and choose **Loop** for repeated playback. On a fresh Pi, use **Upload & Load** to add the tracked file to the Pi's runtime library. The [source waveform](../projects/heart_phantom/profiles/MID102_resp_sample_20s.csv) contains the original sample times. The two-axis Phantom panel expects its own axis 0, axis 1 and DO0 columns.

Datasheets and controlCARD schematics are in [datasheets/](datasheets/). The two subsystem guides own their implementation details. Update the corresponding guide when its interface, wiring or operating procedure changes.
