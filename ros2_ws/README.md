# ROS 2 host workspace

The Pi runs ROS 2 Jazzy. Packages in [src/](src/) provide the bridge, shared interfaces, CSV loader and web relay. The optional [robot packages](../robots/) use the same ROS API as the browser.

| Task | Guide |
|---|---|
| Build, start services, select a project or run in simulation | [Host operations](doc/operations.md) |
| Send commands and interpret applied results and state | [ROS API](doc/ros_api.md) |
| Tune PID or run a step-response experiment | [Controllers](doc/controllers.md) |
| Load tick-by-tick CSV commands or record synchronized signals | [Data Runner and Logger](doc/data.md) |
| Use the Phantom panel or supply another robot package | [Robot integration](doc/robot_packages.md) |

For system architecture, hardware, troubleshooting and verification see the [system guide](../doc/README.md).
