# Hardware — measured values, wiring and the frozen pin map

Everything here is **measured**, not assumed. `firmware/include/pins.h` is the machine-readable half
of this document; when the two disagree, the bench wins and both get corrected.

**Status: nothing measured yet.** This is the deliverable of phase 1 (PLAN-ESP32.md §8).

## To measure and record

| Quantity | Why it matters | Value |
| --- | --- | --- |
| Which GPIO is the microSD CS/DAT3 line — 21 or 3 | Seeed's docs contradict themselves; decides whether D2 is free for throttle capture, and so whether the button has to move onto I²C (§2.2) | — |
| SR315 channel-output logic level | 3.3 V or 5 V is not in the datasheet, and the ESP32-S3 absolute maximum is 3.6 V (§2.5) | — |
| Arrma BLX BEC output on the receiver rail | Expected ≈ 6 V; sets up the buck converter (§2.7) | — |
| Whether the microSD socket has pull-ups on CMD/DAT0 | SD mode needs 10–47 kΩ; add them if init fails intermittently (§2.3) | — |
| Whether this GM10 V3.1 carries a magnetometer, and whether it has config flash | Decides whether VALSET to FLASH is worth attempting (§5.2) | — |
| Whether the Firma 150A ESC puts anything on the throttle wire | Decides the ESC telemetry question, and whether spurious pulses have to be filtered out of the throttle trace (§2.8, §4.3) | — |
| `DYNMODEL` 4 vs 8 on track | Whether the automotive filter lags a 1/8 buggy's accelerations (§5.2) | — |

## Wiring

To document once built: the 6 V BEC → buck → 5 V pad path with the 1S backup LiPo on the BAT pads
(§2.7), the 74LVC2G17 level-shifter for CH1/CH2 (§2.5), the WS2812B on 3V3 (§2.4), and the I²C bus.
