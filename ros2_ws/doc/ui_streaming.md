# UI stream timing and plot gaps

The dashboard receives sampled bridge state over WebSocket (10–50 Hz). If fresh
messages stop arriving, it also requests an HTTP snapshot. Neither transport
drives the 2 kHz controller, runner, or CSV recorder.

## Delayed WebSocket history

A fresh HTTP snapshot can arrive ahead of queued WebSocket messages. Previously,
the browser discarded every message older than its latest UI sequence. This
protected live controls from rolling backward, but also discarded valid plot
history. A temporary transport delay could therefore leave a permanent blank
segment in the ten-second plot.

Delayed messages now fill their original timestamp positions in the plot while
live controls/status retain the newest state. Duplicate timestamps are ignored;
history stays bounded to ten seconds/600 points. Data from before a bridge/UI
restart or an axis mode change is excluded from the new plot. Actual missing
samples still leave a gap when adjacent plotted timestamps differ by >250 ms;
the browser does not synthesize feedback.

## Bench evidence, 2026-11-12

- Laptop WebSocket capture: 0.437 s and 1.185 s delivery pauses, followed by
  buffered messages. Consecutive sample timestamps advanced at most 40.5 ms.
- A subsequent simultaneous comparison measured maximum arrival gaps of 25.1 ms
  for direct ROS, 28.3 ms for Pi-local WebSocket, and 159.3 ms at the laptop.
- The physical recording `2026-11-12_18-17-21.csv` contained 505,086 consecutive
  complete rows in the inspected prefix, with no skipped sample numbers. Host
  telemetry/deadline counters remained 1/5 during stream measurements, and
  dropped samples remained zero.
- The browser's TCP connection showed retransmissions. Pi Wi-Fi power saving
  was enabled; this inspection did not establish its role or change networking.
- The diagnostic proxy (`tests/ui_stall_proxy.mjs`) pauses only WebSocket
  delivery for one second every five seconds. With the fix, the browser
  recovered a plot gap in 765 ms, showed zero remaining gaps at inspection,
  and recorded no main-thread tasks over 50 ms. The unit regression also checks
  ordered insertion, duplicate suppression and retention of genuine gaps.

Evidence: laptop `.local/stream-check-20261112/`; Pi
`.local/stream-check-20261112/pi-streams.json`. The transport can still pause;
the history fix does not guarantee low network latency or recover messages
that never reach the browser.
