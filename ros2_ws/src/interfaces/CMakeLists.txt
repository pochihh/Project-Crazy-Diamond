cmake_minimum_required(VERSION 3.16)
project(interfaces)
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/BridgeState.msg" "msg/DspState.msg" "msg/CommandResult.msg"
  "msg/PositionCommand.msg" "msg/DutyCommand.msg" "msg/DigitalCommand.msg" "msg/AnalogCommand.msg"
  "msg/ControllerType.msg" "msg/ControllerProfile.msg"
  "srv/Command.srv" "srv/DspCommand.srv" "srv/ControllerCatalog.srv"
  "msg/Signal.msg" "msg/DataPreview.msg" "msg/RobotPanel.msg" "srv/SignalCatalog.srv" "srv/ListPrograms.srv" "srv/LoggerCommand.srv" "srv/LoadProgram.srv" "srv/RunnerCommand.srv" "srv/LoadCsv.srv")
ament_export_dependencies(rosidl_default_runtime)
ament_package()
