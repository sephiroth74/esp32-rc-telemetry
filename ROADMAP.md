# Firmware roadmap — step by step

The phase summary lives in [PLAN-ESP32.md](PLAN-ESP32.md) §8; this file breaks every phase into
steps. Section numbers (§) refer to PLAN-ESP32.md.

Every step is small, self-contained and testable, and none is done until:

- `make build` compiles and `make test` is green;
- a measurement it produced is in [docs/hardware.md](docs/hardware.md), and a decision it changed is
  in the plan.

**Order.** What has to be seen working first comes first: navigating the menu (Phase 2), then the
card and GNSS checks of the power-on self-test (Phase 3). Everything after builds on those two.

**Input hardware.** The steps assume the **two-button interim layout** of §3.1 — OK and NAV on the
D0 ladder. Fitting the JS5208 five-way switch is a separate procedure, possible at any point after
step 2.3: see [Switching to the JS5208](#switching-to-the-js5208).

`[x]` done · `[ ]` to do.

## Phase 0 — Bench tools (done)

- [x] **0.1 Display, card and button.** `tools/oled-sd-test`: OLED at 0x3C, SDMMC 1-bit at 40 MHz,
  FAT32 and long names, a 256 KB speed test, a restart button, a GPS page.
- [x] **0.2 GNSS link.** `tools/gps-test` on the shared `tools/lib/gm10`: GM10 found at 115200,
  NMEA and UBX, MON-VER answered.
- [x] **0.3 Results recorded.** Bench session 1 in `docs/hardware.md`.

## Phase 1 — Hardware bring-up and pin-map freeze

- [x] **1.1 Two-button ladder.** Fit the external 10 kΩ pull-up on D0, keep OK straight to GND, and
  add NAV through 10 kΩ. A small sketch, `tools/keys-test`, prints the ADC reading in mV for every
  key and shows the classified key on the OLED.
  *Done when:* the readings for OK, NAV and no key are in `docs/hardware.md`, and `KEY_MV_BOUNDS` in
  `config.h` is derived from them.
  *Result:* OK 0 mV, NAV 1,623 mV, no key 3,120 mV — the ADC saturates there — with no phantom
  presses; the RIGHT/none bound moved to 2,695 mV.
- [x] **1.2 Which GPIO is DAT3** (§2.2). Hold the candidate GPIO low while calling
  `SD_MMC.begin()`. A card that sees DAT3 low at CMD0 enters SPI mode, so the SD-mode init fails:
  failing with GPIO21 low means DAT3 is 21, failing with GPIO3 low means it is 3.
  *Done when:* the answer is in `docs/hardware.md`, and `PIN_SD_D3` and `PIN_RC_THROTTLE` are final
  in `pins.h` — throttle moves to D1 if DAT3 is GPIO3.
  *Result:* **GPIO21**, with `tools/sd-dat3-test`: one candidate low per power-up, since a card
  picks its mode once and ignores DAT3 after that. GPIO3 low mounted, GPIO21 low failed. Throttle
  stays on D2, and D1 stays spare.
- [ ] **1.3 GM10 supply** (§9 question 16). Check the module's input range. If it accepts 3.3 V,
  power it from 3V3 and repeat the `tools/gps-test` link check.
  *Done when:* the answer is in `docs/hardware.md`, and §2.7 says which rail powers the GM10.
- [ ] **1.4 GM10 magnetometer** (§9 question 4, first half). If the module has SDA/SCL pads, first
  check that they idle at 3.3 V and not 5 V, then wire them to D4/D5 and read the I²C scan of
  `tools/oled-sd-test`. The config-flash half needs VALSET: step 4.1.
  *Done when:* answered in `docs/hardware.md`.
- [x] **1.5 What the receiver sends** (§2.5). `tools/rc-scope` reads one channel's signal wire on D3
  through a 10 kΩ/10 kΩ divider, with the ADC as a scope, and plots it in the Arduino IDE's Serial
  Plotter: width, period and level per frame, or the waveform. Only the signal and ground are wired;
  the XIAO runs from USB.
  *Done when:* the logic level, the frame period and the steering and throttle widths of each car on
  the bench are in the per-car table of `docs/hardware.md`, inside the `RC_*` windows of `config.h`
  or with those windows changed.
  *Result:* the Inferno's KR-331 — 3.3 V logic, positive pulses every 16.6 ms (60 Hz), steering
  1,050–1,500–1,940 µs, throttle 1,005–1,500–≈1,940 µs, no telemetry on the throttle wire: all inside
  the `RC_*` windows, which stay as they are. Repeat for each new car.
- [ ] **1.6 Freeze the pin map.**
  *Done when:* `pins.h` holds no `UNRESOLVED`, and the wiring section of `docs/hardware.md` describes
  the bench build.

## Phase 2 — Keys, display and menu navigation (first priority)

Goal: every screen of §3.4 reached and left with the two buttons, on placeholder data. The logic
lives in `lib/ui`, free of `Arduino.h` and host-tested (§6.1); `src/` only adapts the hardware.

- [x] **2.1 Key windows.** `lib/ui`: classify a reading in mV as OK, UP, DOWN, LEFT, RIGHT or no
  key against `KEY_MV_BOUNDS`. A key counts after `KEY_CONFIRM_SAMPLES` equal readings; a reading
  outside every window is discarded.
  *Test:* host tests on synthetic sample streams — clean presses, bounces, two keys at once.
- [x] **2.2 Gestures and layouts.** `lib/ui`: turn the key stream into events — short, long (firing
  at 1.5 s, with the hold-bar progress from 300 ms), auto-repeat for UP/DOWN — and apply the
  two-button mapping: NAV short is DOWN, NAV held 1 s is LEFT. This mapping is the only
  layout-specific code.
  *Test:* host tests for each layout, at the edges of every threshold (799/800 ms, 1499/1500 ms).
  *Result:* `lib/ui/ui_keys.{h,cpp}`, fed from `config.h` through `include/key_config.h`; 20 host tests
  in `test/test_ui`, against the measured ladder levels, including the millisecond counter wrapping.
- [x] **2.3 Keys on the device.** `src/keys.cpp`: ADC1_CH0, read in one-shot mode with the
  calibration curve so the result is in mV, at 100 Hz in the `ui` task. The events of 2.2 go to a
  queue.
  *Test:* on the bench, every press logged on the serial console as the right event.
  *Result:* `src/keys.cpp` and the `ui` task (`src/ui_fsm.cpp`), with `analogReadMilliVolts()` —
  the call the levels were measured with. On the bench every gesture gave its event at the exact
  threshold: NAV back at 1,000 ms, OK hold at 1,500 ms while still held, nothing for OK released at
  1,380 ms; readings OK 0 mV, NAV 1,613–1,628 mV, no key 3,121 mV.
- [x] **2.4 Display driver.** `src/display.cpp`: U8g2 on the hardware I²C at 400 kHz behind the bus
  mutex. Primitives for the status bar, the six body lines, the toast and the hold bar. Only dirty
  pages are sent (`updateDisplayArea`), at most at 5 Hz.
  *Test:* a test screen on the bench. Time a full-frame and a one-page update and record both in
  `docs/hardware.md`: they check the bus budget of §2.4.
  *Result:* `src/display.cpp` and the bus lock `src/i2c_bus.cpp`. Full frame 31.3 ms, one page
  3.9 ms — a little above the plan's estimate, same conclusion. On the bench the toast, the hold
  bar and the 80 ms inversion behave as §3.1–§3.2 say. A missing display is skipped in the code
  (headless boot), but that path is first tested in step 3.2.
- [x] **2.5 Menu model.** `lib/ui`: the menu tree of §3.4 as data — Home, Menu, submenus, dialogs
  (hold OK = yes, any other key or 10 s = no), list and value editors — with the cursor,
  wrap-around, back, and the 30 s fall-back to Home.
  *Test:* host tests that walk paths: Home → OK → Menu → … → Settings › Display → change → OK;
  dialogs answered both ways; both timeouts.
  *Result:* `lib/ui/ui_menu.{h,cpp}` — the lists as data, editors opening on the value in force, the
  refusals of §3.4 (no card, no fix, logging refused, no RC signal), and the two confirmation
  dialogs reachable from the menu. What an entry does comes out as a `Command` for the caller; the
  facts go in through `MenuContext`. 21 host tests in `test/test_menu`.
- [x] **2.6 Screens on placeholder data.** Render every screen of §3.4 from the model, with fixed
  sample data for GNSS, card, track and laps. SESSION, GEOMETRY, CALIBRATION and TRANSFER are stub
  states, entered and left as §3.7 says.
  *Test:* on the bench, every screen reached and left with the two buttons; every text within 21
  columns; the key hints in the two-button wording.
  *Result:* `lib/ui/ui_board` (the modes, their dialogs and toasts) and `lib/ui/ui_screens` (a pure
  render), with 16 host tests in `test/test_board` — one renders every screen and mode in both
  layouts and both text sizes and proves each line fits. On the bench the 5x8 text proved too small:
  `tools/font-test` compared seven fonts, the **6x13B** won, and text size became a setting of its
  own, small or large, with large the default and content that scrolls (§3.2).
- [x] **2.7 The on-device settings, for real.** Display brightness, dim delay and text size applied to
  the panel; the car profile selection stored. Kept in NVS for now — they move into `config.json` in
  step 3.6.
  *Test:* all of them survive a reset.
  *Result:* `src/settings` keeps brightness, dim delay, text size, the profile count and the active
  profile in NVS, checked against their ranges when read back, and writes only what a command
  changed. Brightness is the SSD1306 contrast (16, 96, 255), dimmed is 0. On the bench, low, 15 s,
  small and a New car came back after a reset as `settings: brightness low, dim after 15 s, text
  small, profile 4 of 4`. The bench also asked for the press that brightens a dimmed display to do
  nothing else: `Gestures::swallow()` now drops what the gesture has queued too, host-tested, and
  checked on the bench with OK, OK held and NAV on a dimmed display.
- [x] **2.8 New car and New track** (§3.4, §3.8, §7.1). New car is the choice after the profiles in
  Settings › Car profile; New track an entry of Track, held to create. Both work for any car or
  track that was not set up from a computer, and the apps rename them.
  *Result:* in `lib/ui` with their refusals — no card, no fix, `Full: 8 profiles` — and New car
  landing on Calibrate. Every screen is now tested with the longest track id, `N4512E00934-99`, which
  moved the start-line dialog onto new lines. The placeholder facts carry both out on the bench.

- [x] **2.9 Restart from the menu** (§3.4). Settings › Restart, held: the toast, then a restart and
  the POST. Asked on the bench in place of a key gesture, which the ladder cannot make safe.
  *Result:* works on the bench.

*Phase done when:* a walk through all of §3.4 on the bench, with the two buttons only.

## Phase 3 — Power-on self-test: card and GNSS link (first priority)

- [x] **3.1 POST runner.** The ordered checks of §3.3, one line per check on the POST screen, the
  result bitmask, the fatal latch with the hold-OK retry, the 3 s list of degraded checks, then
  IDLE. The keys are dead throughout. Ordering and severity logic in `lib/ui`.
  *Test:* host tests with fake checks — pass, degraded, fatal, fatal then passing on retry.
  *Result:* `lib/ui/ui_post` — the eleven checks with their severities, the run, the latch and its
  retry, the list worst first, the bitmask and the summary for About — and `renderPost` for its
  screens; 11 host tests in `test/test_post`, every screen within 21 columns at both sizes. On the
  device `src/post` runs it from `setup()` before the ui task, and swallows a key still held at the
  end. Check 0 is the display; the others report `--` until their steps. It reaches Home in 0.8 s.
- [x] **3.2 Checks 0–2.**
  - Display: I²C bus recovery, address ACK, init. A missing display boots headless.
  - PSRAM: the 256 KB ring buffer allocated.
  - LittleFS: mounted with the `littlefs` label (§6.3).

  *Test:* on the bench, with the display unplugged the board still boots, and the serial console
  shows the result.
  *Result:* `i2cBusBegin()` clocks a held SDA free before Wire starts, `log_ring` takes the 256 KB in
  PSRAM once, and LittleFS mounts by its `littlefs` label, formatting a blank partition on the first
  boot. On the bench: `0 Display ok`, `1 PSRAM ok 8MB`, `2 Config ok 8K used`, Home at 0.8 s; with
  the display unplugged, `POST fail 0`, Home at 3.0 s after the list, and the keys still acted.
- [x] **3.3 Checks 3–6, the card.**
  - Mount: SDMMC 1-bit at 40 MHz, with DAT3 held high.
  - Free space: against `sd.min_free_mb` and `sd.warn_free_mb`.
  - Probe: 256 KB written, read back, compared and deleted, timed to give the card speed.

  The logic is the one proven in `tools/oled-sd-test` (`mountSd`, `testSpeed`).
  *Test, on the bench:*
  - no card → fatal 3; insert it and hold OK → passes;
  - exFAT card → fatal 3, naming the cause;
  - nearly full card → degraded 4, logging refused;
  - normal card → its speeds in the result.

  *Result:* `src/card` mounts at 40 MHz with DAT3 high, and when the mount fails asks the card
  itself why — no answer, or a first sector showing exFAT, GPT or no FAT32. On the bench: no card →
  `3 Card FAIL no card`, the latch, the card back in and OK held → `3 Card ok 58GB`, `4 Free ok
  58.2GB`, `5 Write ok`, `6 Speed ok 3276KB/s`. A card left in SPI mode by the DAT3 test was refused
  the same way until unplugged. Not tried for want of the cards: an exFAT or GPT card, and a nearly
  full one, whose thresholds the host tests cover.
- [ ] **3.4 UBX frame scanner.**
  1. Capture raw GM10 traffic into `docs/fixtures/`, with a raw-dump mode in `tools/gps-test`
     that writes the UART bytes to the card.
  2. Write `lib/ubx`: frame sync, length, Fletcher checksum, the raw frame out (§5.4).

  *Test:* host tests against the captured fixtures, including truncated and corrupted frames.
- [ ] **3.5 Check 8, the GNSS link.** The auto-baud scan of §5.2: 230400, 115200, 38400 and 9600
  baud, looking for `0xB5 0x62` or `$G` within 1.5 s per rate. It runs as POST check 8.
  *Test, on the bench:* the link is found at 115200, as the module was delivered. With the GPS
  unplugged: degraded 8, logging refused, Transfer still reachable.
- [ ] **3.6 Results and settings.**
  - `/post.log` written on LittleFS.
  - Settings › About shows the result by check number.
  - `/config.json` read from the card, validated key by key and mirrored to LittleFS (§3.8, check 2).
  - The settings of step 2.7 move into `config.json`.

  *Test:* a `config.json` with a bad key → that key falls back to its default and is listed in
  `/post.log`. With the card removed, the LittleFS copy is used.

*Phase done when:* the board boots through the POST on the bench, and every card and GNSS failure
ends on the screen §3.3 describes.

## Phase 4 — GNSS

- [ ] **4.1 Configuration.** UBX-CFG-VALSET for:
  - NAV-PVT at 10 Hz and NAV-SAT at 1 Hz;
  - NMEA off;
  - 230400 baud;
  - SBAS on;
  - `DYNMODEL` 4.

  It is applied to RAM+BBR and must be acknowledged: that is POST check 9, retried in the
  background. Also try the FLASH layer, and record whether the module accepts it (§9 question 4).
- [ ] **4.2 NAV-PVT and NAV-SAT decode** in `lib/ubx`.
  *Test:* host tests against fixtures captured after 4.1.
- [ ] **4.3 GNSS task.** Core 1, a 2048 B UART receive buffer, a raw-frame queue of 32, loss
  counters (§4.2, §5.3).
- [ ] **4.4 Live GNSS on screen.** The GNSS screen and the status bar fed by real NAV-PVT: the link
  and its baud, the fix and satellites, the coordinates, hAcc and pDOP (§3.4). Measure the time to
  first fix, cold and warm, into `docs/hardware.md`.

## Phase 5 — Logging core and manual sessions

- [ ] **5.1 Record format.** The `.rcl` records written down in `docs/protocol.md` before any code
  (§7.2).
- [ ] **5.2 Packing.** `lib/rcl`, with host tests and golden fixtures shared with `rct_core`.
- [ ] **5.3 Logger task** (§4.4): the PSRAM ring buffer, 32 KB writes, pre-allocated 4 MB extents,
  a flush every 5 s, latency statistics.
- [ ] **5.4 On-card layout** (§7.1): track cell id and resolution, session numbering, `meta.json`.
  A track whose geo-fence contains the fix comes first, then the 300 m radius; among several the one
  used most recently wins, and Track › New track creates a folder at the fix with the next suffix.
  The point-in-polygon test lives in `lib/geo`, host-tested; the GNSS screen names the track.
- [ ] **5.5 Manual sessions** (§3.6): hold OK to start, hold OK and confirm to stop; markers; the
  session screen's GNSS page live.
  *Test:* a 30-minute bench session with clean latency and loss numbers.

## Phase 6 — RC inputs, car profiles, automatic sessions, sleep

- [ ] **6.1 RC front end.** The 74LVC2G17 with the input pull-downs of §2.5. For each car, the logic
  level and the BEC voltage go into `docs/hardware.md`.
- [ ] **6.2 RC capture.** MCPWM capture with pulse validation and reject counters; RC samples in the
  log; POST check 10 (§4.3).
  `tools/rc-live` already times one channel this way on the bench, with the `RC_*` windows, and
  shows it on the OLED as a bar from the centre.
- [ ] **6.3 Car profiles and calibration** (§3.8, §4.3).
  - Profiles read from `config.json` and chosen in Settings › Car profile.
  - New car: a `Car N` profile appended to both copies of `config.json`, the NVS calibration at its
    index cleared, the profile made active.
  - The Calibrate procedure, with its overwrite confirmation.
  - One calibration per profile in NVS, copied into each session's `meta.json`.
- [ ] **6.4 Automatic start and stop** (§3.6). The rules are host-tested in `lib/ui`.
  *Test:* on the bench with the throttle, then on track with speed. Record the thresholds against §9
  question 17.
- [ ] **6.5 Power loss and sleep** (§4.5).
  - SAVING when the RC edges stop, then deep sleep.
  - Wake on OK and on the RC signal.
  - Sleep when idle; a USB host keeps the device awake.

  *Test:* switch the car off mid-session → the file is closed and parseable. Switch it back on →
  the POST runs and the session starts automatically.
- [ ] **6.6 Aux channel trigger** on D1, if §2.2 left it free (§8, deferred).

## Phase 7 — IMU

- [ ] **7.1 LSM6DSO32**, read through its FIFO and sharing the bus with the display under the mutex.
  POST check 7; per-epoch aggregates in the log. Decide on the raw stream (§9 question 7).

## Phase 8 — Track and laps

- [ ] **8.1 Gate crossing.** `docs/algorithms.md` written first; then `lib/geo` gate crossing, with
  host tests on a recorded lap — including a recorded off and a recorded cut (§7.5).
- [ ] **8.2 The automatic start line.** The first lap found without a line, the gate at its fastest
  point, saved with source `auto`, and the session's laps worked out again from it (§7.5).
- [ ] **8.3 Lap flags.** `cut` and `off` against the median lap distance; flagged laps never become
  the best lap.
- [ ] **8.4 Track entries.** Track › Start line, with its overwrite confirmation; Track › Geometry;
  both showing their source, and the source rules of §7.5 when files arrive from the desktop app.
- [ ] **8.5 Live laps and sectors.** The session's laps page, and the last and best laps on Home.

## Phase 9 — Transfer

- [ ] **9.1 USB MSC** from Menu › Transfer. Leaving it while a host is attached needs a confirmation
  (§7.3).
- [ ] **9.2 BLE GATT**, only in TRANSFER: status, control and download per `docs/protocol.md`
  (§7.4), verified against the phone and desktop apps.

## Phase 10 — Field hardening

- [ ] **10.1 Soak test across a race day.** Review write latency and losses, the backup LiPo's
  behaviour, and the JS5208's durability (§9 question 18).

## Phase 11 — Camera (optional)

- [ ] **11.1 Photos, then short clips**, on markers and laps, under the rules of §8, "Phase 11".

## Switching to the JS5208

Possible at any point after step 2.3.

1. Fit the switch on its carrier board: common to GND, OK to D0, and UP, DOWN, LEFT and RIGHT
   through 2.2k, 4.7k, 10k and 22k. NAV's 10k resistor stays, as LEFT's.
2. Measure each key with `tools/keys-test`; update `KEY_MV_BOUNDS` and `docs/hardware.md`.
3. Set `"keys": { "layout": "five" }` in `/config.json`. Nothing else changes: the mapping of step
   2.2 is the only code that knows which layout is fitted.
