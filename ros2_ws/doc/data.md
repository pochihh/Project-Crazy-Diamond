# Data logger, runner and CSV library

All clients operate the same backend state. A ROS node may start an experiment,
the browser will display it, and either may stop it. Conflicting starts are
rejected; one client cannot silently replace another client's recording/run.
There is one active recorder and one loaded program per bridge.

## Recorder

Use `/data/signals` to get the complete signal catalog, names, units, groups and
numeric/text flags. It includes six-axis control/feedback, controller parameters,
DSP diagnostics, encoder counts, DIO/AIO, host RT counters and runner position.
Millimeter fields for channels without position calibration are not meaningful;
record their encoder counts and `position_configured` flag instead.

`/data/logger` Start chooses columns; an empty list means all catalog fields.
The UI requires a selection. `timestamp_us` and `sample` are always present.
Selection stays fixed for that file. Text fields can be recorded but not plotted.
The record rate is 2 kHz while valid synchronized snapshots are available; host
CPU/memory/resource-page measurements are separate low-rate diagnostics, not
part of the realtime signal catalog.

Files are written outside RT to `recordings/YYYY-MM-DD_HH-mm-ss.csv`, using Pi
local time. A collision adds `_1`, `_2`, etc.; no file is overwritten. CSV comments
include timezone offset, project, period, calibration, origin, bounds and profile.
Select controller gain/limiter signals to preserve live parameter changes per row.
Stop closes and flushes files before download. DSP reset closes the current segment.
A runner fault closes its associated recording. An independent logger can continue
recording valid fault-state feedback until explicitly stopped. Closed recordings
do not restart automatically.

`/data/preview` carries latest/min/max values in catalog order for each drained
batch. The web relay retains bounded recent batches, so a short pulse's extrema
survive ordinary display-rate reduction. This is a preview, not an alternative
full-rate recording. Disconnected/overloaded clients may lose preview history.

## Program files and CSV contract

The persistent library is `data/programs/` in the canonical Pi checkout. It is
runtime data, ignored by Git; source-code pulls do not remove it. Back up experiment
inputs and recordings separately. The Data Runner and phantom panel default to
selecting a saved file. Upload optionally adds/replaces a named file. Replacement
must be explicit. File names are local basenames ending in `.csv`.

The bench library includes `phantom_example.csv`: a 2-second period, ±1 mm on
axes 0/1 in phase, with a 5 ms DO0 command pulse at row zero. It is a reusable
example, not an automatically started experiment.

One data row is **one 0.5 ms tick**, beginning at row zero. No time column, variable
sample rate, interpolation or fuzzy header matching is used. All recognized
columns in a row are applied together before controller execution.

| Exact headers | Meaning |
|---|---|
| `axis0_reference_mm` … `axis5_reference_mm` | Position reference passed to that axis's controller; requires calibration |
| `axis0_duty` … `axis5_duty` | Signed normalized DSP duty, −1…+1, constrained by configured cap |
| `do0` … `do3` | Digital output, exactly 0 or 1 |
| `ao0_raw`, `ao1_raw` | DAC code, integer 0…4095 |

Header whitespace and a UTF-8 BOM are tolerated. Blank lines and `#` comments are
ignored. Unknown columns generate warnings and are ignored. Recognized values
must be finite numbers; blank/short rows, duplicate channels, invalid codes and
simultaneous duty/reference columns for one axis prevent loading. These are basic
parsing checks, not a trajectory-validation framework. Python parses CSV; C++
checks the typed program against current hardware ranges before acceptance and
checks the current row again during execution.

Limit: 32 MB input text and 2,000,000 recognized numeric values. Programs are
preloaded into immutable memory outside RT. No disk access occurs during playback.
A library file survives restarts; the loaded program and active run do not.
Reference columns can exceed the UI slider range; they are checked for finite,
representable DSP encoder counts rather than against host travel limits.

```csv
axis0_reference_mm,axis1_reference_mm,do0
0.0,0.0,1
0.1,0.2,1
0.2,0.4,0
```

## Start, completion and stop

Start selects modes from column types. `auto_enable=true` enables required axes
and system I/O after normal interlock checks; it does not clear faults. Wrong-mode
already-enabled axes require disable first. Channels not listed are untouched,
except starting from a globally disabled system does not activate unrelated
previously selected motors.

| Completion | Behavior |
|---|---|
| `hold` | Hold the last values and retain channel ownership; associated recording continues until Stop |
| `stop` | After the last row's tick, disable owned motors, zero owned DO/AO, close associated recording |
| `loop` | Next tick executes row zero again; no inserted blank tick |

Runner Start with `record=true` prepares the file before execution and begins the
record ID and row zero at the same worker boundary. Runner Stop also ends that
associated recording. Logger Stop alone only stops recording. Standalone manual
recording is not implicitly claimed by a runner started with `record=false`.

Manual conflicting writes are rejected while running or holding. Parameter
changes within the active controller type remain available. Stop All cancels
runner drive. Link/controller faults interrupt the runner. A missed deadline only
interrupts it when the project's `disarm_on_deadline_miss` policy is true; otherwise
the next row runs on the next tick, stretching elapsed timing without skipping
rows or catching up with bursts. DSP disarm/recovery still follows the normal
bridge gate. Every reported overrun/missed period remains logged under either policy.
