# RC Telemetry

GPS logger and analysis suite for an ARRMA Typhon 6S 1/8 buggy: a device on the car records position
at 10 Hz plus driver inputs and chassis motion, detects laps on-device, streams live data to a phone
and keeps full-resolution logs for offline analysis.

Three deliverables, one repository:

| Path | What |
| --- | --- |
| [firmware/](firmware/) | pioarduino / ESP32-S3 firmware for the on-car device |
| [packages/](packages/) | shared Dart core: `.rcl` format, geometry, lap math |
| [apps/](apps/) | Flutter phone app (live) and macOS app (analysis) |
| [docs/](docs/) | the authoritative format, hardware and algorithm documents |

## Planning

- [PLAN.md](PLAN.md) — system-level context, hardware, cross-project decisions, repository layout.
- [PLAN-ESP32.md](PLAN-ESP32.md) — the on-car device in detail. Each project gets its own document.

## Building the firmware

The `pio` CLI is not on `PATH`; the [Makefile](Makefile) addresses it explicitly.

```shell
make build     # compile
make upload    # flash over USB-C
make monitor   # serial console
make test      # host unit tests for firmware/lib/
make help      # everything else
```

**Status: skeleton.** The tree and the build configuration are in place; the modules are empty and
land phase by phase per PLAN-ESP32.md §8. Phase 1 is hardware bring-up, which has to settle the two
open pin questions before the map in `firmware/include/pins.h` can be frozen.
