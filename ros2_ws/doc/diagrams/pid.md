# PID

```mermaid
block-beta
    columns 9
    ref["Reference"] err(("Σ")) space pid["PID"] space limit["Duty cap"] space g["G · motor"] y["y"]
    space neg(("−y")) space:6 encoder["Encoder"]
    ref -- "r" --> err
    err -- "e" --> pid
    pid -- "P + I + D" --> limit
    limit -- "u_g" --> g
    g --> y
    y --> encoder
    encoder --> neg
    neg --> err
    classDef signal fill:transparent,stroke:none
    class y signal
```

Reference applies the selected passthrough or slew limit. PID uses Kp, Ki, Kd and the configurable derivative filter. The duty cap and motor polarity determine the outgoing command.

The matched CSV channels carry the reference, input measurement, error, signed P/I/D contributions, controller output and motor duty command. The encoder position in the DSP response is a newer observation than the measurement used to calculate the acknowledged command.

Signals: `ctrl_r_mm`, `ctrl_y_mm`, `ctrl_error_counts`, `ctrl_p_duty`, `ctrl_i_duty`, `ctrl_d_duty`, `ctrl_u_duty`, `ctrl_u_g_duty`, `requested_duty`, `output_duty`, `position_mm`, `ctrl_valid`
