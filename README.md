# RC Telemetry

GPS logger and analysis suite for RC cars: a device that moves between cars records position at
10 Hz plus driver inputs and chassis motion, detects laps on-device, and keeps full-resolution logs
that a phone app reviews and a desktop app analyses between runs.

Three deliverables, one repository:

| Path | What |
| --- | --- |
| [firmware/](firmware/) | pioarduino / ESP32-S3 firmware for the on-car device |
| [packages/](packages/) | shared Dart core: `.rcl` format, geometry, lap math |
| [apps/](apps/) | Flutter phone app (downloads, configuration, laps) and macOS app (analysis) |
| [docs/](docs/) | the authoritative format, hardware and algorithm documents |

## Planning

- [PLAN.md](PLAN.md) — system-level context, hardware, cross-project decisions, repository layout.
- [PLAN-ESP32.md](PLAN-ESP32.md) — the on-car device in detail. Each project gets its own document.
- [PLAN-UI.md](PLAN-UI.md) — one-page overview of the device's keys, board states and screens.
- [ROADMAP.md](ROADMAP.md) — the firmware phases broken into small, testable steps.

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
land phase by phase per PLAN-ESP32.md §8. Phase 1 is hardware bring-up: the microSD, display and
button already work on the bench (`docs/hardware.md`), and the microSD DAT3 question still has to be
settled before the map in `firmware/include/pins.h` can be frozen.
