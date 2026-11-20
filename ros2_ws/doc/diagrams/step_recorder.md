# Step Recorder

```mermaid
flowchart TD
    r["r = 0 mm"] --> e(("−"))
    e -->|"error in counts"| p["P · tunable Kp and motor sign"]
    p -->|"u"| sum(("+"))
    step["Baseline → step → recovery"] -->|"u_im · signed duty"| sum
    sum --> cap["Axis duty cap"]
    cap -->|"u_g"| motor["DSP output → motor G"]
    motor -->|"position"| y["Encoder and calibration"]
    y -->|"−y"| e
```

The step is in signed motor duty units and is added after feedback polarity. The motor input is the sum after applying the axis duty cap. Kp = 0 gives open-loop excitation. The DSP reports its own applied duty.

Signals: `ctrl_r_mm`, `ctrl_y_mm`, `ctrl_error_counts`, `ctrl_u_duty`, `ctrl_u_im_duty`, `ctrl_u_g_duty`, `requested_duty`, `output_duty`, `position_mm`, `ctrl_valid`
