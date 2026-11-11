# Real-time diagnostics and stability checks

Updated: 2026-11-11. The earlier intermittent fault was not reproduced. The owner
accepted normal operation with persistent overrun/missed-period diagnostics.

## Observed fault

Before the controller-profile deployment, the bridge had one latched telemetry
fault and seven missed 500 µs deadlines after about 28 minutes. Maximum cycle
work was 591.836 µs; maximum wakeup lateness was 69.286 µs. The old handler discarded
the exception text, so this does **not** establish electrical SPI corruption:
invalid CRC/header, repeated telemetry, missing acknowledgement and transport
errors all used the same UI message.

Evidence: `.local/controller-results/state-before.json`. The subsequent short
acceptance tests passed, but did not resolve this longer-run observation.

## Instrumentation

`recordings/realtime-events.jsonl` on the Pi now records each physical telemetry
exception or missed deadline, including:

- Exact exception text, ioctl result/errno, and complete transmitted/received
  96-word frames.
- The preceding eight cycles: host monotonic start time, execution/SPI/wakeup
  timing, DSP frame/time/acknowledgement, command count, operation and arm mask.
- The number of missed 500 µs deadlines. A delay spanning several periods counts
  every missed period in both the event and the UI's `deadline_misses` counter.
  Normal microsecond-scale wakeup lateness is measured, but is not a missed tick.

The 2 kHz thread only fills fixed-size structures and a bounded queue. The normal
ROS thread writes and flushes events and journal messages. The queue holds 255 events;
if it overflows, aggregate lost-context event/period counts are logged explicitly.
File-write failure is reported in the journal, where event summaries continue.
This changes neither the normal 2 kHz rate nor the 500 µs fault threshold.
The transport also checks for a short ioctl result, in addition to negative errors.
An initial empty pipeline frame can still be reported while outputs are disabled.

After an overrun, the next exchange waits a full period instead of starting
immediately. This allows DSP DMA/CS rearming and avoids a compressed recovery
exchange. The original overrun is still counted and logged; an active motor still
trips the existing host deadline fault. This recovery correction is **not** proof
of the earlier fault's cause.

Inspect with:

```bash
journalctl -u diamond-bridge --since today
cat recordings/realtime-events.jsonl
```

The DSP's lifetime `invalid_frames`, `rejected_commands` and `watchdogs` include
deliberate endpoint fault tests. Compare their **increments**, not their absolute
values, with the host's counters, which restart with the bridge process.

These are diagnostics for the host's 2 kHz schedule and received SPI frames.
The current protocol does not expose a separate counter for missed CPU1 5 kHz
interrupts, so the host does not claim to log every internal DSP interrupt miss.

## Bench configuration

- Ubuntu 24.04.4, `6.8.0-2049-raspi-realtime`, Raspberry Pi 5.
- Control worker: CPU 3, FIFO 80, locked current memory. Normal ROS/UI work: CPUs 0–2.
- Boot isolation: `isolcpus=managed,domain,3 nohz_full=3 rcu_nocbs=3 irqaffinity=0-2`.
- SPI0 uses RP1 `dw_spi_mmio`, 10 MHz, mode 1, 16-bit words, 192-byte messages.
  The BCM2835 polling-limit setting does not apply to this controller.
- Baseline DMA IRQ `dw_axi_dmac_platform`: IRQ 142 on this boot, thread FIFO 50,
  affinity 0–2. SPI calls use the immediate synchronous path; the `spi0` pump
  thread is not handling these messages.
- Baseline governor: `ondemand`. No reported thermal/undervoltage throttling.

## Reproduction test

Run on the Pi, from the checkout, with detached shafts and the physical bridge
already connected, fault-free, and not recording:

```bash
taskset -c 0-2 python3 -u tests/realtime_soak.py --physical --load \
  --seconds 1800 --output .local/rt-results/loaded-soak
```

This moves both axes through ±5 mm at an eight-second period, records every
2 kHz sample, opens eight 50 Hz UI streams, and runs CPU loads on cores 0–2.
It fails on any new host error/deadline/drop or DSP invalid/rejected/watchdog
counter. It checks the completed CSV for missing/duplicate samples and increasing
timestamps, then leaves outputs disabled and restores duty limits/control modes.
Per-run snapshots and results are stored under the chosen output directory.
The test resets both axis zeros at its start; it does not restore the old zeros.

## Results

- **Five-minute loaded run passed:** 600,243 consecutive CSV samples, zero new
  host telemetry errors/deadline misses/drops and zero new DSP invalid frames,
  rejected commands or watchdog events. Eight streams each received about 14,674
  messages. An additional 1 GiB SD write burst also caused no error.
- Every recorded acknowledgement advanced by one. DSP frame steps were always
  two or three, consistent with sampling its 5 kHz snapshots at 2 kHz.
- Two further loaded runs were intentionally interrupted at user checkpoints,
  with 818,357 and 661,240 recorded samples and unchanged error counters. These
  are **not** completed 35-minute soak passes.
- Before the final logging deployment, the instrumented process had received
  **2,887,482 frames** over about 24 minutes without a spontaneous telemetry
  exception or missed deadline. Its maximum cycle was 406.241 µs.
- **Logging injection checks passed with outputs disabled:** six deliberate
  1.3–4.1 ms host pauses generated six records accounting for 31 missed periods.
  A separate 2 ms preemption during execution generated one record for four
  missed periods. Event totals matched the live counter; both JSON and journal
  logging worked, with no secondary SPI errors. These seven records/35 periods
  remain in the log as intentional tests. The bridge was then restarted, clearing
  only its live counters.
- **Final normal-load regression passed:** both axes ±50 mm and return, hold,
  Reset zero, ±3% PWM and over 60 seconds of recording. **130,164 consecutive
  samples**, zero telemetry errors/deadline misses/drops. Maximum cycle
  **293.297 µs**, maximum wakeup lateness **17.539 µs**.
- All 11 ROS tests passed, including new deadline-boundary/multiple-period counts
  and event-history serialization checks. Synthetic CPU loads, extra WebSocket
  clients and temporary kernel tracing were removed. Both services remain running
  with outputs disabled; the owner's live PID parameters were restored.
- A subsequent idle health check still had zero faults/misses/errors/drops. Cores
  0–2 used 4.6%, 3.6%, and 6.1%; core 3 used 10.1%. The process-wide maximum had
  since reached 409.149 µs (32.052 µs maximum wakeup lateness), within 500 µs.

No permanent governor/IRQ-affinity changes or DSP firmware changes were made.
The old intermittent fault's cause remains unproven; the evidence above is bounded
bench coverage, with logging now available for a recurrence.

Evidence is preserved in `.local/rt-results/`: `final-build.log`, the three run
directories, `before-final-deploy.json`, `injected-pauses.json`,
`injected-execution-overrun.json`, `diagnostic-journal.txt`,
`final-regression.json` and `final-health.json`. Full recordings remain on the Pi.

## DSP reboot and frozen counters (2026-11-11)

A later intentional DSP power cycle exposed a host reset-recovery limitation.
The bridge had started at 14:29:27 and accepted 51,717 samples before the DSP
reset. It then rejected the new, lower DSP timestamp/sequence as stale. ROS
continued publishing, but `connected` was false and the sample number and DSP
diagnostics remained at their last accepted values (31/243/7/466).

The captured incoming SPI frames proved that the DSP had rebooted: timestamp and
sequence restarted, session/ack were zero, and invalid-frame/watchdog/encoder
error counters were zero. The bridge retained its old session and continued
sending disarm commands; these accumulated as rejected commands on the DSP.
Restarting the bridge after DSP boot restored fresh telemetry with zero host
errors/misses and zero DSP invalid-frame/watchdog/encoder errors. The rejection
total had reached 473,293 and then stopped increasing.

For a clean power-cycle sequence: stop `diamond-bridge`, power-cycle the DSP,
allow FLASH boot, then start `diamond-bridge`. A bridge restart alone does not
clear DSP lifetime counters. DSP fault clear and axis Reset zero do not clear
these counters either. The CPU2 maximum polling gap is a timing measurement,
not an error count, and normally returns to about 2 µs after reboot.

Automatic epoch/session recovery and clearer labeling of cached DSP data remain
unimplemented. This reset event does not establish the cause of the earlier
spontaneous telemetry fault. Evidence: `.local/reset-investigation/`.
