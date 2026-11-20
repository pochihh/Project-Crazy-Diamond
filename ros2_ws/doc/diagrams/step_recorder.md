# Step Recorder

```mermaid
block-beta
    columns 9
    space:4 inj["Step u_im"] space:4
    r["r = 0"] err(("Σ")) p["P · Kp"] space mix(("+")) cap["Duty cap"] space g["G · motor"] y["y"]
    space neg(("−y")) space:6 encoder["Encoder"]
    r --> err
    err -- "e" --> p
    p -- "u" --> mix
    inj --> mix
    mix --> cap
    cap -- "u_g" --> g
    g --> y
    y --> encoder
    encoder --> neg
    neg --> err
    classDef signal fill:transparent,stroke:none
    class r,y signal
```

The step is in signed motor duty units and is added after feedback polarity. The motor input is the sum after applying the axis duty cap. Kp = 0 gives open-loop excitation. The DSP reports its own applied duty.

Signals: `ctrl_r_mm`, `ctrl_y_mm`, `ctrl_error_counts`, `ctrl_u_duty`, `ctrl_u_im_duty`, `ctrl_u_g_duty`, `requested_duty`, `output_duty`, `position_mm`, `ctrl_valid`
