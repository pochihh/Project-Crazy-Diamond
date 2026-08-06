# Project Crazy Diamond
- A realime control suite for control researchers

## Project documents

- [Implementation roadmap](IMPLEMENTATION_PLAN.md)
- [Electronics and DSP pin assignment](electronics.md)
- [TMDSHSECDOCK point-to-point wiring table](wiring_table.md)
- [SPI DMA protocol](spi_protocol.md)
- [ROS 2 architecture, topics, and services](ros2_interfaces.md)
- [Legacy DSP implementation history](DSP_DEV_HISTORY.md)

## Project scope
- A real-time master controller on a preemptable RT Linux system, on a Raspberry Pi5 for example
- Run CPU3 on the RPi (or in the future, a general PC) is isolated, for high-level real-time compuatation
- The system runs real-time ROS2, and communicates with one or multiple DSP using SPI (or EtherCAT in the future) for encoder reading, motor controls, and general ADC/GPIO read/writes.
- The DSP, currently C2000, 28379D model. Which is not EtherCAD capable. We can use the same control structure and update to 28388D (EtherCAD capable) in the future.
- We are using SPI DMA on the 28379D. The reorganized RAM-only firmware projects
  are under `dsp/f28379d/`; the older working reference remains under
  `reference/SPI_Slave_Motor_Control/`.

### DSP requirements
- Current hardware use the control card EVM--TMS320F28379D controlCARD--with TMDSHSECDOCK for wiring. In the future we will use customized PCB board. Hopefully all features will be available with both TMDSHSECDOCK and customized PCB
- The datasheet is under /doc. We also have a working non-DMA code in reference/SPI_Slave_Motor_Control
- One DSP should support: 
    - 6 motor encoder readings (the first three use the already-wired eQEP1–3 pins; the other three use CPU2 eCAP edge interrupts)
    - 4 additional optional encoder-only sensor readings on CPU2 eCAP/XINT interrupts, for slower applications such as linear motion detection
    - ADCA0–5 reserved for future current feedback and ADCB0–5/ADCC2–3 reserved
      for future auxiliary analog inputs; all remain disabled in the current PCB profile

## ROS2 structure
- In the ROS2, there will be a real-time node (bridge node, C++) communicating with the DSP (SPI DMA for now, EtherCAT in the future)
- Ideally we use different SPI master unit (I think there are multiple in RPi) to do the SPI
- There will be a non-realtime node hosting a web-based UI. It's in Python and communicate with the bridge node using ROS topics
- In the web-based UI we can read all status of each DSP, see online data, send commands, etc.
- CPU/CLA:
    - CPU1: main code, 5 kHz coherent encoder snapshot, homing/safety, and SPI slave DMA
    - CPU2: three motor encoders at `×4` using eCAP1–6 plus four auxiliary
      encoders at `×2` using XINT1–4, with a coherent 5 kHz IPC snapshot to CPU1
    - CLA1: six 5 kHz position/velocity discrete transfer-function controllers, with `NA = NB = 16`; duty mode bypasses the controller
    - CLA2: unused


## First pass scope

### DSP
- Update DSP code from the DMA example, adding all new features (refer to the reference code for CLA and )
- Assign IOs properly, strictly make sure it's compatible with TMDSHSECDOCK for now. We may adjust DSP requiremnt if it's hard to achieve. For example reduce the number of motors we can support.
- Keep separate RAM-only CCS executable projects for CPU1 and CPU2 during early
  iteration. Add Flash configurations only after the RAM/HIL path is stable.
- Wiring table/diagram so I can wire the DSP to RPI, encoders, H-bridges, etc. We use jumper wire for now. In the future we will create custom PCB based on this 

### RPI ROS2
- Test suites for the DSP DMA
- ROS2 infra, use docker. Create laptop env so we can develope the ROS2 code on the laptop
- ROS2 nodes:
    - brdge: C++, real-time node that bridges the DSP to the system with ROS2 topics. The node is running on the isolated CPU3. The high-level control algorithm should also be running here. I am not sure if this is a good structure since we also need to run the ROS2 services. I am not familiar with real-teim node in ROS2, discuss with me. Performance of this node is key in this project.
    - ui-brdge: Python backend serving the web-based-UI. Refer to the UI strucutre from this project: https://github.com/pochihh/Project-NUEVO
        - We should include a simple login feature (reuse the one in Project NUEVO), a basic setup/config so we can assign encoder to motors.
        - Real-time motor plot, reuse the one in (reuse the one in Project NUEVO). The data saving feature here might need to be changed. THe record button should somehow toggled the a flag in the the real-time node and push real-time data in a shared memory or something. A non-RT function will then export the data in a local file, then when the download is clicked in the web-UI, we download that file. The Project NUEVO strucute is we push all data to the UI and cache it there, but now the real-time data might be to much. We will stream ~50 Hz to the UI; the first-pass Pi/SPI loop is 1 kHz and DSP control is 5 kHz.
        - table of all IO data, etc
    - robot: Python or C++, computes kinematics and generate reference command to the bridge. Also do reads/write. Can be controled through the web-based-UI
