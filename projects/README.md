# Project configuration

Select the project with `DIAMOND_PROJECT` at host build and launch. The default is [heart_phantom](heart_phantom/).

| Project file | Used by |
|---|---|
| `bridge.yaml` | Bridge startup parameters, including axis calibration, direction, default controller profiles, duty limits and missed-deadline policy. |
| `project.json` | Dashboard title and axis descriptions. Array order matches DSP channels. |
| `controllers/*.yaml` | Named, reusable tuning profiles loaded and saved by the bridge. |
| `profiles/*.csv` | Tracked experiment inputs. Upload a playback CSV to the Data Runner to put it in the Pi's saved-program library. |

Compiled controllers live in the ROS [bridge](../ros2_ws/src/bridge/include/bridge/controllers.hpp). The optional robot panel and experiment coordinator live in [robots/](../robots/). See [controller profiles](../ros2_ws/doc/controllers.md) and [host configuration](../ros2_ws/doc/operations.md#configuration-and-manual-operation).
