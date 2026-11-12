# SPI framing investigation — 2026-11-12

Status: captured and narrowed down; no transport or firmware fix applied.
Times below are America/Los_Angeles. Host/DSP source revision: `0235e11`.

## What increased

Compare the recovery snapshot after the earlier Pi outage with the 14:15:02
snapshot, without resetting either device's counters:

| Counter | Recovery baseline | 14:15:02 |
|---|---:|---:|
| DSP invalid frames | 5 | 7 |
| DSP rejected commands | 0 | 0 |
| DSP command timeouts | 2 | 2 |
| DSP encoder errors | 0 | 0 |
| Host telemetry errors | 0 | 2 |
| Host missed periods | 5 | 38 |
| Host dropped samples | 0 | 0 |

The two host errors occurred at 14:13:27, worker cycles 744035–744036. Both
reported `Invalid v5 DSP telemetry`; the SPI ioctl returned all 192 bytes with
errno zero. The host latched its telemetry fault and resynchronized. At inspection
the link was connected, DSP fault was zero, and all outputs were disabled.
The changed host session is consistent with resynchronization, not a DSP reboot.

The latest run started at 14:12:45 and recorded 6,384 samples / 3.1914 seconds.
The errors happened roughly 39 seconds after that run stopped. The trace already
shows motor mask zero before either bad frame. The three recent CSVs
(`14-12-07`, `14-12-21`, `14-12-45`) all have complete rows and consecutive sample
numbers. This incident therefore does not establish a PID or robot Stop failure.

## Frame evidence and timing hypothesis

- Both captured host commands have valid CRCs.
- In the first bad response, words 1–19 exactly echo command words 0–18.
  The telemetry header appears at word 24 rather than the nominal word 4.
  Its 89-word payload cannot fit in the remaining 96-word transaction.
- The second response has no telemetry header. Neither response is recoverable
  by the existing alignment search and CRC checks; those checks must remain.
- The preceding cycle took 436.753 µs. Only 26.482 µs elapsed between its work
  completion and the next worker iteration. This is a host timing measurement,
  **not** a measured chip-select-high interval.

The leading hypothesis is that a short turnaround after a slow host exchange
overlaps DSP DMA rearming. In `cpu1/spi_dma.c`, the CS interrupt validates the
received frame, swaps buffers, and then rearms DMA. That interrupt can wait for
the non-nesting 5 kHz tick; a previous firmware acceptance measured a maximum
tick duration of 62 µs. The host currently waits a full extra period only after
an actual deadline overrun. A cycle that finishes just before its deadline has
no guaranteed minimum turnaround interval.

The delayed command echo is consistent with the TX starvation observed during
the earlier DMA-priming investigation. It does not by itself identify the exact
missed CS edge or prove that this is the same defect. No CS/SCLK scope capture or
DSP ISR timing measurement was taken during this incident. Electrical effects
and other DMA/FIFO failure modes are not ruled out.

Next discriminating test: with outputs disabled and firmware unchanged, measure
the CS turnaround and compare a host minimum-idle guard against the current
scheduler near the 500 µs boundary. Keep logging every missed period and retain
the SPI/DSP disarm behavior. Do not claim a cure from a short error-free run.

## Evidence and current limits

Pi evidence: `.local/dsp-errors-20261112-141502/` contains before/after state,
service/kernel journals, and full event frames. Original events remain in
`recordings/realtime-events.jsonl`. The recovery baseline is in
`.local/robot-stop-20261112-140731/recovered.json`.

Across a further 828,913 worker samples during inspection, frame/error counters
did not increase, but six more host periods were missed while outputs were
disabled. The timing issue remains open. The kernel logged no new warning and
the current-boot throttling flag was zero; neither observation proves power
integrity. No PID, DSP firmware, scheduling policy, or fault gate was changed.
