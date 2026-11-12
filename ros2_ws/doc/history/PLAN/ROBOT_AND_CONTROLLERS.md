> Historical snapshot. Current system documentation: [index](../../README.md).

# Robot packages and controller profiles

Updated: 2026-11-11

Status: the DSP firmware milestone is complete. The PID registry, reusable YAML
profiles, per-axis settings panel and live parameter tuning are implemented in
the 2026-11-11 ROS 2 pass; see [profile operation/development](../PROJECTS_GUIDE.md).
External robot packages and LQR remain proposed/pending.

Confirmed during discussion: controllers remain on the Pi; the DSP is the
duty/encoder/I/O/protection endpoint, superseding DSP A/B controllers. The robot
package owns its embedded CAD/control panels, with a small PCD manifest contract
and ROS 2 command/state interface. The first dock allocation is 4 DI, 4 DO, 6 AI,
and 2 AO; select unused pins and document their electrical ranges.

Confirmed on 2026-11-11: changing controller type requires the
affected axis disabled; tunable parameters, including PID gains, must support
online adjustment while the axis runs. Saving a shared profile does not change
other axes until they explicitly reload it. The owner will provide the LQR state model;
do not assume a position/velocity model or implement LQR before that is supplied.

## Accepted direction

PCD is the reusable axis/I/O device and control infrastructure. A separate robot
package owns the robot node, kinematics, CAD/model assets, robot-level control
methods, robot panel, and connections to external computers or input devices.
The complete robot section of the dashboard comes from that package. A new robot
must not require edits to PCD's dashboard or robot-specific logic in its RT loop.

Axis controllers are compiled C++ implementations. Initially offer PID and LQR;
adding another implementation requires rebuilding the host and adds a selectable
controller type. Tunable profiles are project-specific YAML files, independent
of the implementation and reusable by multiple axes. Each axis opens a controller
settings panel in the existing settings-area position. The panel selects a
controller/profile, displays and edits its parameters, and supports **Save** to
the current profile and **Save as** a new profile. Final styling comes later.

The former project controller alias is replaced by the compiled registry. PID is
available; LQR is not selectable until its model and implementation are supplied.
Saving profiles persists parameters; startup assignments still live in bridge.yaml.

## Proposed responsibility boundary

| PCD core | Separate robot package |
|---|---|
| DSP firmware, SPI codec, hardware capabilities and diagnostics | Robot node, kinematics, coordinated motion and task logic |
| Axis calibration, channel mapping, bounds, motor polarity and output limits | Axis selection/mapping into robot joints |
| Deterministic host controller execution and command arbitration | Robot-space inputs and conversion into axis references |
| Compiled per-axis controller implementations | Robot-specific controller extensions when needed |
| Profile loading, tuning, persistence and recording | Project's YAML profiles and robot launch configuration |
| Direct-axis UI, I/O UI, system/resources, global Stop All | Robot CAD renderer and robot control panel |
| Stable ROS command/state interfaces | Optional network/gamepad/device integrations |

Robot code submits references through the same PCD command path used by direct
axis controls. It does not acquire SPI, write motor hardware, or bypass axis
limits. The existing browser-disconnect behavior remains: losing a page does
not disarm the running host controller. Robot-source loss needs its own explicit
hold/stop policy when robot control is added.

Prefer a small explicit source selection (manual/robot/CSV) over an ownership
framework. Changing source discards pending references from the old source.
Global Stop All continues to act directly on PCD, regardless of whether the robot
panel is available. A coordinated robot command should carry all affected axis
targets together; independent per-axis requests are not a coordinated command.

## Proposed minimal robot/UI contract

Use a versioned robot manifest with a stable robot ID/name, required PCD API
version, axis mapping, and URLs for a robot-view panel and robot-control panel.
The package may serve both from one small web application. PCD places them in
the two existing dashboard slots. An absent package leaves those slots empty or
as placeholders while the direct-axis/system UI remains operational.

Recommended first implementation: the robot package renders its own CAD and
controls in embedded panels. PCD only defines panel sizing, ready/error reporting,
and a small versioned theme/unit context message. No arbitrary UI-schema editor,
runtime JavaScript module loader, or robot-kinematics execution inside PCD's
browser bundle is needed. If messages cross an iframe boundary, check the
configured source window/origin and message version.

ROS 2 is the robot-to-PCD command/state interface. A package may run on the Pi or
another computer, with its own adapters for external input devices. The current
deployment deliberately uses localhost ROS discovery; remote robot-node support
will need an explicit deployment change. This is separate from browser panel
embedding and does not require changing the existing trusted-LAN access policy.

## Controller/profile contract

Keep calibration, encoder/motor polarity, travel bounds and hardware duty limits
in the axis configuration. A reusable tuning profile must not silently change
another axis's wiring, scale or permitted travel. A profile includes a stable
ID, schema version, compiled controller type and that type's tunable parameters.
The axis configuration selects a profile ID. Runtime controller state is private
to each axis even when profiles are shared.

Each compiled controller supplies its parameter names, units, defaults and bounds
so the same settings panel can edit PID, LQR and later controllers. Keep this a
small compiled registry; do not introduce dynamically loaded libraries or an
online code editor. Validate a complete candidate profile outside the RT thread,
then apply it at a control boundary. Preserve finite-output checks and duty caps.

Implemented PID tuning/application and persistence rules:

- Require the affected axis disabled when changing controller implementation/type.
  Reset controller state on that change.
- Support live parameter tuning on the running controller, including PID gains.
  Validate the complete update outside the RT loop and apply it atomically at a
  control boundary. Do not reset all controller state on each tuning update;
  define integrator/observer continuity per implementation to avoid an unintended
  output jump. A profile reload that changes type requires disable; a same-type
  reload follows the live parameter-update path.
- Editing and applying parameters changes that axis's working copy. Mark it
  unsaved until Save or Save as succeeds.
- Save writes the named YAML atomically. Other axes already using it keep their
  active copies until explicitly reloaded; show that their saved profile changed.
- Save as creates a new profile and selects it for the edited axis. Never share
  integrator/observer state between axes.
- Unknown controller types, invalid parameters or unsupported profile versions
  produce an actionable error rather than silently falling back to PID.
- Record the applied controller type and complete parameter snapshot with CSV
  sessions, so later edits to the YAML cannot change the meaning of old data.

PID initially preserves the proven count-based gains. Any move to physical-unit
gains needs an explicit profile version/conversion; displaying mm must not change
the tuning. Controller sample period is part of the contract and must agree with
the actual host loop.

The owner will provide the LQR state model. Once supplied, agree on state and
reference units, measured/estimated states, sample period, gain representation,
and any integral/feedforward terms before implementing it. An online Riccati
solver, automatic identification and generic observer editor are not requested.

## Decisions to discuss before implementation

1. Owner-supplied LQR model and its estimator/reference/units contract.
2. Robot command-source loss policy and the first required remote-input device.

Disabled-only controller changes, online parameter tuning and explicit reload
of a shared saved profile are accepted, not open decisions. Robot panels remain package-owned embedded panels.

The remaining external robot architecture follows the controller-profile pass.
`projects/heart_phantom/` still supplies PCD configuration and display metadata;
robot nodes, model assets and embedded panels move to the separate robot package
when that integration is implemented.
