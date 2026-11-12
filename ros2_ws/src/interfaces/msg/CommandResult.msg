# Discrete command completion at an RT cycle boundary. Service acceptance only queues it.
uint64 command_id
bool applied
string message
# Actual configuration captured by the RT worker immediately after this command.
# Present for controller/profile/gain/duty-limit commands, including rejections.
bool has_controller
uint8 axis
uint64 controller_revision
ControllerProfile controller
float64 duty_limit
