# Troubleshooting

| Observation | What to check |
|---|---|
| DSP frame errors or rejected commands | In **Resources** or `/dsp/state`, compare counter **changes** over the interval of interest. Invalid frames count receive/framing or decode failures. Rejected commands decoded successfully but failed an endpoint rule. The DSP keeps lifetime counts until it reboots. |
| Pi reports invalid telemetry or a missed tick | Check `journalctl -u diamond-bridge` and `recordings/realtime-events.jsonl` on the Pi. Compare host errors with DSP counter changes, session, acknowledged sequence, cycle duration and missed periods. Verify the CE0 branch and single SPI owner. Intermittent v5 framing errors have been observed. A short inter-frame turnaround is one possible cause and has not been confirmed on a scope. |
| Motor stops while the duty meter is high | Compare host selection with the DSP's arm mask, fault, requested duty and reported duty. The meter shows the command accepted by the DSP. The MC33926 board does not return motor-terminal voltage, current or its SF fault signal. If the DSP is armed and reporting duty, the driver inputs, supply and motor terminals distinguish an electrical shutdown from a host stop. |
| Encoder validity is `0x07` instead of `0x3F` | CPU1's three eQEP channels are available. Check that CPU2 is running the matching image and producing fresh snapshots. Validity indicates the acquisition path is live; an unplugged encoder can still report a constant position. |
| UI plot pauses or has a gap | Check the downloaded CSV's `sample`, `timestamp_us` and dropped-sample count. The browser receives lower-rate previews over the LAN. A missing display batch leaves a gap. The bridge writes full-rate recordings on the Pi. |
| A DSP reset interrupts an experiment | Wait for the bridge to resynchronize. Inspect the host and DSP faults, clear them while disarmed, and verify each new axis origin before enabling. A recording interrupted by the reset is closed. |

For pin numbers and probe setup see the [DSP hardware guide](../dsp/f28379d/README.md). For the meaning of individual CRC, timeout and fault fields see the [wire reference](../dsp/DSP_FINITE_STATE_MACHINE.md). [Host operations](../ros2_ws/doc/operations.md) covers service restarts and deadline policy.
