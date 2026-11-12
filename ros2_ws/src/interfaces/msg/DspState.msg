# Decoded protocol-v5 telemetry. Names, units and values match PcdTelemetry.
# No host enable requests, calibrated units or host error counters belong here.
uint32 timestamp_us
uint32 sequence
uint32 session
uint32 command_sequence
uint16 state
uint16 arm_mask
uint16 fault
uint16 encoder_valid
uint16 digital_in
uint16 digital_out
uint16 analog_valid
uint16 analog_out_valid
uint16[6] analog_in
uint16[2] analog_out
int32[6] position
int32[6] reference
float32[6] requested_duty
float32[6] duty
uint32 invalid_frames
uint32 rejected_commands
# Firmware calls this field watchdogs; these are SOFTWARE COMMAND TIMEOUTS.
uint32 command_timeout_count
uint32 encoder_errors
uint16 cpu2_max_poll_us
