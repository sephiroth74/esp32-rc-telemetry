# RC Telemetry — GPS logger & analysis suite for RC cars

## PIOArduino ESP32-S3 Data Logger — Specifications & Firmware Architecture

## Context

There is no existing code: this is a greenfield, three-part system built around a custom device that
mounts on **any electric RC car** and moves between cars, rather than being built for one model.

### Target cars

Nothing about a particular make or model is assumed. What the device needs from a car:

- A receiver that outputs **standard servo PWM** (1–2 ms pulses) on its steering and throttle channel
  ports — the common denominator of surface receivers.
- A **BEC rail** to draw power from, anywhere between 5 V and 8.4 V (PLAN-ESP32.md §2.7).
- Room for the device, and a mounting spot where the GNSS antenna sees the sky.

The values that differ from car to car are measured once per car and recorded in
`docs/hardware.md`.

**Problem.** Driving an RC car on a track today gives no objective feedback. Lap times come from a
stopwatch, and there is no way to know *where* time is lost, whether a lap was clean, or how a setup
or battery change affected performance.

**Goal.** A small logger on the car records position at 10 Hz plus driver inputs and chassis motion,
detects laps and sectors on-device, and stores full-resolution logs that a phone app reviews and a
desktop app analyses in depth. Nothing is streamed while the car is driven: the phone connects over
BLE between runs (PLAN-ESP32.md §7.4).

**Outcome.** Three deliverables in one monorepo: firmware (pioarduino/ESP32-S3), a phone app
(Flutter/BLE) for downloads, configuration and lap review, and a desktop analysis app (Flutter/macOS) that share one Dart core implementing the
binary log format and all track/lap math.

### Decisions taken

| Decision | Choice |
| --- | --- |
| Extra channels beyond GPS | **Steering + throttle** (PWM tap on the receiver's two channel ports) and **IMU** (accel+gyro, I²C). Pack-voltage sensing **out of scope** for now. |
| Local UI | **A five-way navigation switch and a 0.96" 128×64 I²C OLED** — the switch on one ADC pin, the display on the shared I²C bus, no status LED (PLAN-ESP32.md §3) |
| Phone link | **BLE only in Transfer mode** — no live stream, no radio transmitting near the receiver while driving (PLAN-ESP32.md §7.4) |
| Desktop transfer | **USB Mass Storage** — device mounts as a disk over USB-C |
| Track data | **Start line and geometry optional.** Without a start line the gate goes at the fastest point of a lap; the desktop app builds the geometry from the sessions and refines the start line, and writes both back into the track folder, so the next session there has them. Laps that cut or leave the track are flagged, never best (PLAN-ESP32.md §7.5) |
| Track recognition | **By position, never by hand.** Inside a geo-fence drawn in the apps, otherwise within 300 m of the start line or centre; the most recently used wins a tie. The track recognised is every session's default, and the GNSS screen names it (PLAN-ESP32.md §7.1) |
| Names | **Given in the apps, never typed on the device.** A track found on its own, or made with Track › New track, is named by its cell id, and a profile made with New car is `Car N`; the apps rename them in `track.json` and `config.json` (PLAN-ESP32.md §3.8, §7.1) |
| Repos | **Single monorepo** |
| Language | **Everything in English** (code, comments, docs, UI) |

## Hardware findings that constrain the design

Verified during planning:

- **Seeed Studio XIAO ESP32-S3 Sense has BLE 5.0 only — no Bluetooth Classic.** The phone link must be BLE/GATT; a
  serial-port (SPP) style protocol is impossible.
- **From the receiver, only power and PWM are portable.** Telemetry, data ports and serial outputs
  (SBUS, IBUS, CRSF, …) vary by brand or are absent, so the device relies on nothing but the
  receiver's **power** and the **PWM signal on each channel port**. A channel port is simply
  *signal / +V / GND* carrying a standard 1–2 ms servo pulse.
- **XIAO ESP32S3 pinout**: D0–D10 → GPIO 1,2,3,4,5,6,43,44,7,8,9. All of D0–D10 are ADC-capable.
  Default I²C = D4/D5, SPI = D8/D9/D10, UART = D6/D7. User LED = GPIO21.
- **Sense expansion board microSD**: CLK GPIO7, DAT0 GPIO8, CMD GPIO9 (the SPI SCK/MISO/MOSI pads),
  **DAT3/CS GPIO21** per Seeed's official code examples (`#define SD_CS 21`). ⚠️ The same Seeed wiki
  page *also* states the slot "occupies GPIO3 (CS)" — the docs contradict themselves, and the bench
  has not settled it yet (PLAN-ESP32.md §2.2). It decides whether D2 is free.
- **Verified on the bench** (`docs/hardware.md`): SDMMC 1-bit runs at **40 MHz** with no external
  pull-ups, at 3.5 MB/s write and 2.9 MB/s read. Cards must be **FAT32 on an MBR partition table**,
  at any capacity — a 64 GB card works — while exFAT and GPT are not supported by the core's FatFs.
  Long file names work.
- XIAO has **BAT pads with onboard LiPo charging** → we can build a true UPS (see Power).
- Latest pioarduino platform: **`55.03.311`** (Arduino core 3.3.11 / ESP-IDF 5.5.5).
- Local toolchain: Flutter 3.47.2, Dart 3.13.2, pioarduino VSCode ext 1.4.4, `~/.platformio` present.
  **`pio` CLI is not on PATH** — add it (`~/.platformio/penv/bin`) for scripted builds/tests.

---

## Repository layout

```shell
rc-telemetry/
  pubspec.yaml               # pub workspace root (Dart 3.13 native workspaces)
  Makefile                   # build / test / format entry points
  docs/
    protocol.md              # SINGLE SOURCE OF TRUTH: .rcl binary format + BLE GATT
    hardware.md              # measured voltages, logic levels, wiring, pin map
    algorithms.md            # gate crossing, s/d projection, cut detection
    fixtures/                # golden .rcl + expected JSON, shared by C++ and Dart tests
  firmware/                  # pioarduino project
    platformio.ini
    partitions.csv
    include/pins.h           # the one place pin assignments live
    src/ ...
    test/                    # native (host) unit tests for the geo/lap code
  packages/
    rct_core/                # pure Dart: format, geo, track, analysis  (no Flutter)
    rct_charts/              # Flutter: linked-cursor CustomPainter charts
    rct_ui/                  # Flutter: shared widgets, theme, map layers
  apps/
    mobile/                  # Flutter iOS/Android — downloads, configuration, lap review
    desktop/                 # Flutter macOS — analysis workbench
```

Use **native pub workspaces** (`workspace:` in the root `pubspec.yaml`, `resolution: workspace` in
each package) rather than melos — Dart 3.13 supports them natively, giving one lockfile and one
`.dart_tool`. A `Makefile` covers task running.

Each project will have a separate markdown file for its own planning:

- **PLAN-ESP32.md** for arduino/esp32 project.
