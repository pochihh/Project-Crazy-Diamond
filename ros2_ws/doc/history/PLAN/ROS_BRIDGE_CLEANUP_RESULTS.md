> Historical snapshot. Current system documentation: [index](../../README.md).

# ROS bridge cleanup acceptance — 2026-11-11

## Scope

The bridge accepts continuous ROS topic inputs, selects direct duty or its
in-process controller, and publishes a canonical DSP telemetry message. The web
adapter relays that state and routes operator input. Publisher silence holds
the last accepted value; stop/disable/fault behavior remains enforced.

The DSP firmware and v5 wire protocol are unchanged. No boot ID, reset reason,
hardware-watchdog setup or DSP reboot command was added. The Pi-side SPI code
now resynchronizes sessions before checking timestamps from a new DSP boot.
See [current architecture and operator/API guide](../ROS2_WS_GUIDE.md).

## Automated and mock checks

- Pi ROS build: all three packages built; **14 tests passed**.
- UI: **6 tests passed**, TypeScript check and production build passed.
- Core tests: latest-value coalescing, concurrent mailbox integrity, source and
  enable gates, no stale-input replay after stop/re-enable, publisher-silence
  hold, separate host/DSP fault clear, session-zero DSP reboot recovery and
  within-session stale-frame rejection/counter wrap.
- ROS-only integration with the web node absent: position, direct duty, publisher
  exit hold, explicit stop, I/O-only operation and exact raw-state relay passed.
- Direct-only mock startup with all position controllers disabled and an empty
  profile directory passed; channel 5 accepted duty without a PID profile.
- Mock HTTP/recording, live profile tuning/reload/isolation and I/O tests passed.
  Mock scheduling is ordinary Linux scheduling; its deadline counts are not a
  physical real-time acceptance measurement.

## Physical tests

The actual two-axis system passed:

| Check | Result |
|---|---|
| ROS-only control with `diamond-ui` stopped | PASS: small position target, brief 1.5% duty, hold after publisher exits, explicit stop, I/O |
| HTTP/UI adapter position and ±3% duty regression | PASS: 8,922 consecutive recorded samples, no host errors/missed deadlines/drops |
| Live PID tuning, shared-profile isolation, explicit reload, CSV snapshots | PASS: 5,123 samples, no host error/deadline/drop increments |
| I/O-only commands and recorded telemetry | PASS: 809 samples, no motors armed; this does not remeasure pin voltages |
| 60-second loaded two-axis run | PASS: 120,163 consecutive samples, all counters below unchanged |

The loaded run used three CPU workers on cores 0–2 and eight simultaneous
50 Hz WebSocket clients while both axes followed ±5 mm targets and recorded at
2 kHz. Maximum worker execution was **322.853 µs**, below the **500 µs** period;
maximum observed start lateness was **25.013 µs**. Each web client received
approximately 2,903 messages. All load workers were stopped afterwards.

Counter increments during the loaded run:

| Counter | Increment |
|---|---:|
| Host telemetry errors | 0 |
| Missed control deadlines | 0 |
| Dropped sample-queue entries | 0 |
| DSP invalid frames | 0 |
| DSP rejected commands | 0 |
| DSP command timeouts | 0 |

These hardware/load tests used `36063d5`. The subsequent `d58f562` change removes
redundant top-level telemetry aliases and updates their UI/test consumers; it
does not change worker control, SPI or timing logic.

The deployed `d58f562` build passed all 14 ROS tests and 6 UI tests again. Its
physical HTTP/position/duty regression recorded **8,913 consecutive samples**,
with zero telemetry errors, missed deadlines or dropped samples. Maximum worker
execution was 278.779 µs and maximum start lateness was 24.117 µs. The ROS-topic
regression also passed again, including hold after publisher exit, explicit
stop, I/O and exact raw DSP relay. The final API check confirmed the nested
`dsp` representation and removal of the old duplicate fields.

The DSP's pre-existing rejected-command total was **473,293** and did not
increase during the loaded test or final regressions. These are firmware
lifetime counters, not counts since the bridge service started. Fault clear
does not reset them.

Both system services are running from the canonical Git checkout. Tests left
system enable off, all duties/DO/DAC requests zero and recording stopped.

## UI inspection and evidence

Browser inspection confirmed the DSP panel's live reported state, session and
acknowledgement, arm/fault masks, invalid/rejected frame counters, command timeout
count, encoder diagnostics and polling gap. The fixed Stop All header, original
axis sliders and two axis cards per row remain. DSP fault clear is separate from
host fault clear. Unsupported firmware reboot is not presented as an action.

Evidence is on the Pi under
`/home/toby/Project-Crazy-Diamond/.local/ros-cleanup/`: `physical-ros.txt`,
`physical-http.json`, `physical-controllers.json`, `physical-io.json`,
`final-http.json`, `final-ros.txt`, `loaded/result.json`, `loaded/states.jsonl`,
and the corresponding mock reports.
Recordings remain in the canonical checkout's ignored `recordings/` directory.

## Physical DSP power-cycle follow-up

The operator subsequently performed several intentional DSP power cycles while
both Pi services stayed running. The bridge observed **three DSP restarts**,
each with a new time origin and a fresh host session. It reconnected without
restarting either service; their process IDs remained unchanged. Outputs stayed
disabled and the host communication fault remained latched until explicitly
cleared. Startup zeros were captured again after synchronization.

An active CSV recording ended on the first restart with the explicit message
`Recording ended because the DSP restarted; a new startup zero was established`.
Its **388,326 samples** have consecutive sample numbers and strictly increasing
timestamps; no post-reset time origin was mixed into that file. Recording did
not resume automatically. The original monitor assumed exactly one restart, so
that assertion failed; the operator confirmed multiple power cycles, matching
the three observed boot sequences.

After the final reconnection, a 30-second disarmed observation passed with no
additional host errors, DSP frame errors, restarts or missed deadlines. Clearing
the host fault left outputs disabled. The subsequent physical position and
±3% duty regression passed and recorded **8,915 consecutive samples**, then left
system enable off, motor/DO/DAC outputs zero and recording stopped.

| Counter | Before power cycles | After cycles and control regression |
|---|---:|---:|
| Host-observed DSP restarts | 0 | 3 |
| Host telemetry errors | 0 | 5,308 |
| Missed control deadlines | 0 | 0 |
| Dropped sample-queue entries | 0 | 0 |
| DSP invalid frames | 0 | 1 |
| DSP rejected commands | 473,293 | 0 |
| DSP command timeouts | 0 | 0 |
| DSP encoder errors | 0 | 0 |

The host telemetry errors were accumulated while DSP feedback was unavailable;
the count remained 5,308 through the stability and control checks. The DSP's one
invalid-frame count also remained unchanged after recovery. Its old rejected
command count reset with DSP power cycling, while host counters persisted in
the still-running bridge. Maximum worker execution for that service lifetime
was 348.317 µs, already present before this test, below the 500 µs period.

Evidence is in the same Pi directory: `power-cycle-baseline.json`,
`power-cycle-states.jsonl`, `power-cycle-result.json`,
`power-cycle-stability.json`, `power-cycle-cleared.json` and
`power-cycle-control.json`. The DSP firmware was unchanged throughout.

## Coverage limits

This is bounded regression evidence, not a long-duration reliability claim.
DSP restart recovery now has both host state-machine tests and physical
power-cycle coverage with outputs disabled. Power loss during active motor
drive was not tested. Firmware standalone FLASH boot was previously verified
in [stage 3](STAGE3_RESULTS.md). No motion tests were added for physical motor
channels 2–5. Reset-cause capture and hardware-watchdog recovery remain deferred.
