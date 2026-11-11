# Distinctions
- Project Crazy Diamond (PCD): the reusable DSP/ROS2 infrasctautrue, system, and functions
- Project/ my project:
    - Specific robot proeject, with ceratin motor/encoder configurations, robot design/kinematics, control algorithm, some system specific feautres and customzied ROS2 nodes and topics
- We need to make a clear separation about the 2 scope, so it's clear what and where to modify when creating a new project. But we don't need to over complicate things. The user will always install the source code. So we don't need to make PCD a stand alone software.

# UI layout
## Title area
- ESTOP stays at all time. This is only software based, hardhard estop is still needed.
- Show current project name (e.g., for this first project it's Project/Heart Phantom)

## Tab bar
- Only icon by default, shows tab name on hover.
- A setting (web-wise) on the bottom. Put theme, website related frontend/backend performance settings here. Include one to toggle showing front end performance status

## The 3 tabs
### Dashboard (sections listed in a column order)
- A place holder for the robot (will be a 3D rendering), and on the right side the direct control handles
- DSP list
- Axis list
    - Use progress bar for duty, use a toggle switch for enable (swtich background turns green when enabled)
    - We need a clear way to design the controller. Simple PID is just for tesing, we will need a away to implement more complicated ones.
    - Control mode should incldue position and pwm for now. We will use pwm to do system id, and then design the controller.
    - Review the whole layout to make it more clean and clear
### System
- Rpi status: CPU/memory/disk usage and size; GPU usage if applicable; refreshed timely
- ROS2 nodes: RT/non-RT, finished late counts, etc
- ROS2 topics, collpase to open details, with realtime streaming (at least 10 hz?)

### Resources
The table is not so clear. I also want a more clen
