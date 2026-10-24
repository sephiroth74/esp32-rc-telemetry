# Hardware — measured values, wiring and the frozen pin map

Everything here is **measured**, not assumed. `firmware/include/pins.h` is the machine-readable half
of this document; when the two disagree, the bench wins and both get corrected. Section numbers
(§) refer to PLAN-ESP32.md.

**Status: phase 1 in progress** (§8). The microSD, display, button, GNSS-link and key-ladder results
below come from the first bench session; the open items follow them.

## Bench session 1 — microSD, display, button, GNSS link, key ladder (2026-10-16)

Setup: XIAO ESP32-S3 Sense, pioarduino platform `55.03.311` (Arduino core 3.3.11 / ESP-IDF 5.5),
sketch `tools/oled-sd-test`.

### microSD over SDMMC

Card: **Lexar microSDXC 64 GB**, UHS-I, U3 / V30 / A1, rated up to 100 MB/s read (Amazon ASIN
B08XZ2KS1F).

| Quantity | Value |
| --- | --- |
| Bus | SDMMC 1-bit: CLK GPIO7, CMD GPIO9, DAT0 GPIO8, with GPIO21 (DAT3) driven high before `SD_MMC.begin()` |
| DAT3 | **GPIO21**, settled by `tools/sd-dat3-test`: held low at the first CMD0 after power-up, GPIO21 sends the card to SPI mode and the mount fails, while with GPIO3 low the card mounts (59,700 MB). Seeed's wiki, which says GPIO3, is wrong. A card picks its mode once per power-up and ignores DAT3 afterwards, so each try needs the USB unplugged: a first version that tried all three in one boot found every one mounting |
| External pull-ups on CMD/DAT0 | **Not needed.** Init and data transfers are clean at 20 MHz and 40 MHz (§2.3) |
| 40 MHz (`SDMMC_FREQ_HIGHSPEED`) | **Works.** 256 KB written in 32 KB chunks, read back and compared: no mismatch |
| Write / read, 32 KB clusters | **3,495 KB/s / 2,912 KB/s** — 256 KB, the close and flush included in the write time |
| Write / read, 512 B clusters | 1,106 KB/s / 1,551 KB/s — same card, same 40 MHz bus |
| Long file names | **Work.** `/rc-telemetry long filename test.txt` was written, read back and deleted, so the §7.1 layout stands |

Both 32 KB-cluster figures exceed 2.5 MB/s, the ceiling of a 1-bit bus at 20 MHz, so high-speed mode
really is engaged. The core offers no way to read the negotiated clock, so the throughput is the
evidence.

**Filesystem: FAT32 on an MBR partition table, nothing else.** The core's FatFs is built with
`FF_FS_EXFAT 0` and `FF_LBA64 0`: an exFAT volume fails to mount, and a GPT disk is not readable.
Capacity is not the limit — this 64 GB SDXC card works once formatted FAT32.

**Cluster size: 32 KB**, which is what macOS Disk Utility picks for this card (MS-DOS (FAT), Master
Boot Record). A card formatted *by the ESP32* gets 512-byte clusters, because the Arduino `SD_MMC`
wrapper passes an allocation unit of 0, and it writes three times slower, as the table shows. The
firmware therefore never formats a card itself.

How to read a mount failure:

- `mount_to_vfs failed (0xffffffff)` — the card answered in SD mode but holds no FAT volume it can
  read. Usually exFAT, or a GPT disk.
- `0x107` (`ESP_ERR_TIMEOUT`) — the card never answered at all.
- Do not fall back to SPI after an SDMMC attempt. A card that has been initialised in SD mode does not
  enter SPI mode without a power cycle, so the fallback fails even on a healthy card.

### Display

| Quantity | Value |
| --- | --- |
| Module | 0.96" 128×64 OLED, SSD1306-compatible, I²C; rows 0–15 yellow, 16–63 blue (QIQIAZI, Amazon ASIN B0F13SSSFB) |
| Wiring | VCC → 3V3, GND → GND, SDA → D4 (GPIO5), SCL → D5 (GPIO6) |
| Address | **0x3C** (7-bit). Listings giving 0x78 / 0x7A quote the 8-bit form: 0x78 = 0x3C << 1, 0x7A = 0x3D << 1 |
| On-module pull-ups | Present, to the module's VCC (SDA read high against the internal pull-down). Hence 3V3 only: on 5 V they would pull the bus above the ESP32-S3's 3.6 V maximum |
| Driver | Adafruit SSD1306 2.5.17 in the bench tools; **U8g2** in the firmware (§2.4) |
| Full frame, 1 KB, U8g2 at 400 kHz | **31.3 ms** (`sendBuffer()`, averaged over 4) |
| One 8-row page, 128 B | **3.9 ms** (`updateDisplayArea()`, averaged over 4) |
| Text, small | `u8g2_font_5x8_mr` on a 6-pixel grid: 21 columns × 8 rows, one row per page. Readable, but too small at arm's length |
| Text, large | **`u8g2_font_6x13B_mr`** on the same grid: 21 columns, a status line in the yellow band and three body rows. Picked with `tools/font-test` from seven candidates (5x8, 6x12, 6x13B, 7x13B, 8x13B, spleen8x16, 9x15B): the largest that keeps 21 columns |

### Button

D0 (GPIO1) to GND with `INPUT_PULLUP`, no external resistor: works.

### Key ladder — two-button interim (§3.1)

Sketch `tools/keys-test`, which classifies with the `KEY_MV_BOUNDS` of `config.h` itself. Wiring:
10 kΩ from D0 to 3V3, OK from D0 straight to GND, NAV from D0 through 10 kΩ to GND. The ADC is read
with `analogReadMilliVolts()` at 100 Hz.

| Key | Min | Avg | Max | Nominal | Over |
| --- | --- | --- | --- | --- | --- |
| OK | 0 mV | 0 mV | 0 mV | 0 V | 20 presses, 275 samples |
| NAV | 1,609 mV | 1,623 mV | 1,637 mV | 1.65 V | 20 presses, 250 samples |
| none | 2,910 mV | 3,120 mV | 3,121 mV | 3.3 V | 10,570 samples |

- **Every press was seen once and released once**: 40 presses, no phantom or double events with
  two equal samples (20 ms) required to accept a key.
- **The ADC saturates at about 3.12 V**, not 3.3 V, so "no key" reads 3,120 mV. That moved the
  RIGHT/none bound from 2,785 to 2,695 mV; the 2,910 mV minimum is a single sample during a
  release, still 215 mV clear of it.
- **Without the 10 kΩ pull-up the pin floats** (927 mV, and keys pressed by noise every few hundred
  milliseconds): the ADC driver disables the internal pull-up, so the external one is required.
- **Two buttons, one shared resistor** — a button to 3V3 and a button to GND meeting before a
  common resistor — floats at rest and shorts 3V3 to GND when both are pressed. The ladder needs its
  resistor to 3V3 permanently in place.

### GNSS link — Flywoo GOKU GM10 Nano V3.1

| Quantity | Value |
| --- | --- |
| Wiring | GPS TX → D7 (GPIO44), GPS RX → D6 (GPIO43), 5V → 5V, GND → GND; UART1 |
| Baud rate as found | 115200 |
| Output as found | NMEA and UBX together, both at **10 Hz**: NAV-PVT and nine NMEA sentence types each tenth of a second (captured into `docs/fixtures/gm10-indoor-115200.hex`, 2026-10-19) |
| Both UART directions | **Work**: the UBX-MON-VER poll is answered |
| UBX-MON-VER | sw `ROM SPG 5.10 (7b202e)`, hw `000A0000`; `FWVER=SPG 5.10`, `PROTVER=34.10`; `GPS;GLO;GAL;BDS`, `SBAS;QZSS` |
| Fix | Indoors: none, 0 satellites. At a window (2026-10-22): **3D, 10 of 15 satellites, hAcc 0.75 m, pDOP 3.5** — a partial sky |
| Time to first fix, cold | **141 s** from holding OK on the GNSS screen at the window, about **255 s from power-on**: the module had been on the bench unpowered for two days. The figure is the GNSS screen's `Fix`: 3D within 5 m, held 3 s (§3.4) |
| Time to first fix, hot | **5 s** from power-on, the same 3 s hold included, after 16 s unplugged: the module's own backup kept what it had learnt |
| Constellations at the window | NAV-SAT lists GPS, Galileo, BeiDou and SBAS, no GLONASS (off on the M10 by default): 24 satellites, 17 heard, 10 in the solution — 4 GPS, 3 Galileo, 3 BeiDou. The 4 SBAS were tracked, their corrections not yet applied (`diffSoln` 0) |
| Output in the firmware | 10 Hz to the epoch: 100 NAV-PVT in every 10 s for over half an hour, **0 lost** (gnss task, step 4.3) |
| Configuration as found | Four of the eleven keys of POST check 9 differed: NAV-SAT off, NMEA on, `DYNMODEL` other than 4, 115200 baud. The other seven — 10 Hz, NAV-PVT with every solution, UBX out, SBAS with differential corrections — were already as §5.2 wants them (step 4.1, 2026-10-20). The old `DYNMODEL` value went unrecorded: that boot's log was lost to the USB re-enumeration |
| Config flash | **Yes.** It takes a VALSET to the FLASH layer, and VALGET of that layer returns all eleven keys with their values: the four written there, and seven that were in it already — Flywoo's own setup lives in flash, which is why it survived days unpowered |
| BBR | Kept over **15 s unpowered**: the module has a backup supply of its own, and 16 s off still gave a hot fix in 5 s. **Gone after two days** off: the four keys written there were lost. How long it lasts in between is still to measure. VALGET of the BBR layer reads configuration keys only, and answers NAK when it holds none — it says nothing about the navigation data kept there |

**TX and RX swapped give no byte at all**, at any baud rate, which looks like a dead module. Found
on 2026-10-19 after the bench was rewired for the RC tests: the multimeter read 3.28 V on the line
from the XIAO, idle, and 2.50 V on the module's TX, which landed on D6 against the XIAO's own TX —
two outputs fighting, averaged by the meter. A software swap of the pins did not bring the link up,
so put the wires right rather than the pins: GPS TX to D7, GPS RX to D6.

`PROTVER=34.10` confirms the configuration-database interface that §5.2 relies on (`CFG-VALSET`).
The `ROM` prefix says the firmware runs from the chip's ROM; the configuration has flash all the
same (above), so the prefix said nothing about it.

### Lesson: solder the headers

Pins pushed through the XIAO's holes without solder make intermittent contact. The I²C lines read
connected on one boot and open on the next, and the display never answered until the headers were
soldered. Every bench result above is from soldered headers.

## Bench session 2 — sessions at a window, off the 5V pad (2026-10-24)

Setup: the device carried to a window and run without the computer, on a **2S LiPo through a
step-down giving 4.90 V, 47 µF across its output, into the 5V pad and GND**. The XIAO boots and the
GM10 runs from that pad, USB-C unplugged. The two must never be connected together: with nothing
between them, the step-down and the computer's VBUS would meet on the 5V pad. The car has no diode
there either, so the same rule holds in it (PLAN-ESP32.md §2.7). Firmware 0.1.0-dev; every log read
back on a Mac by a reader written from `docs/protocol.md`.

| Session | Started | Fix in the session | Filed | Log |
| --- | --- | --- | --- | --- |
| id 4, 176 s | Indoors, no fix; carried to the window | 3D at 111 s, hAcc ≤ 5 m at 123 s; 1.19 m and 8 satellites at the end | Moved at the close into **`N4732E00851`, a new track** made at the fix | 1,941 records, every CRC good, 0 bytes skipped; NAV-PVT at 10 Hz, NAV-SAT at 1 Hz; 0 UART errors, 0 ring overflow; 38 writes, at most 7 ms |
| id 5, 230 s | Indoors, no fix; carried to the window | 3D at 203 s, but hAcc never under 21 m before the stop | **Stayed in `_unassigned`**, as §7.1 says: no fix as §3.4 defines it, 3D with hAcc ≤ 5 m held 3 s | 2,532 records, 0 skipped, 0 lost; 50 writes, at most 7 ms |
| id 6, 121 s | At the window, on a 3D with hAcc 17 m: not yet a fix | hAcc ≤ 5 m at 51 s; 1.70 m and 7 satellites at the end | Moved at the close into **`N4732E00851`, the existing track**, recognised within its 300 m, as `session_002` | 1,335 records, 0 skipped, 0 lost; 28 writes, at most 7 ms |

- **`start.utc` of a session begun before the clock was set** is derived at the close (§7.1): id 5
  reads `09:57:57Z`, which is its last NAV-PVT, 10:01:47 UTC at 229.9 s, less 229.9 s.
- **The status bar's 3D is not the fix.** At this window a first 3D comes with hAcc around 20 m, and
  takes about 50 s to reach 5 m; the files follow the GNSS screen's `Fix`, not the status bar's `3D`.
- **The GM10's epochs are irregular at two moments only**: when the module learns the time, from
  its default 2021-03-07 to the GPS week and then the date, and at its first 2D fix. There, two epochs
  are 102 and 98 ms apart in id 5, and one is 195 ms after the previous in id 4: the module skipped
  an epoch. Every other step is 100 ms, with no error counted on our side.
- **Two earlier sessions cut by unplugging the battery mid-run** (ids 2 and 3, no fix) read to their
  last record. Their 4 MB extent is not cut to length and `meta.json` has no footer, as expected
  without SAVING (§4.5).
- **Every time on the card is UTC** (§7.1), so the Finder shows the files' dates two hours early
  here in summer.

## Bench session 3 — half an hour of logging (2026-10-24)

The 30-minute session of ROADMAP.md step 5.5, on the bench: USB power, indoors with no fix, so the
GM10 sends its NAV-PVT and NAV-SAT as on track, invalid but at the same rates. Started and stopped by
hand; the console logged throughout, and the card was read back on a Mac.

| Quantity | Value |
| --- | --- |
| Length | **32 min 55 s**: session id 7, `_unassigned/session_005` |
| Records | 21,726, every CRC good, 0 bytes skipped; the file cut to its 2,386,864 bytes at the close |
| GNSS | 19,749 NAV-PVT, each 100 ms after the last, none missing; 1,975 NAV-SAT. 0 UART overruns, 0 UART errors, 0 bad checksums; 0 lost in each of the console's 224 windows of 10 s |
| Writes | 426, the longest **13 ms**, the 99th percentile 7 ms, none over 250 ms |
| Ring | 0 overflow; at most **6,700 bytes** waiting, of 256 KB |

## Still to measure

| Quantity | Why it matters | Value |
| --- | --- | --- |
| Whether this GM10 V3.1 carries a magnetometer | §9 question 4; its other half, config flash, is answered above | — |
| How long the module's own backup keeps its BBR, and the time to first fix with the backup LiPo fitted | Hot in 5 s after 16 s off, cold after two days (above): where in between does it go cold? (§2.7) | — |
| Whether the GM10 runs from 3V3 | On battery the 5V pad is dead, so a GM10 on 5 V loses its BBR in SLEEP and every run starts cold (§2.7, §9 question 16) | — |
| JS5208 ladder: the ADC reading of each key, once the switch is fitted | UP, DOWN and RIGHT are still nominal in `KEY_MV_BOUNDS` (§3.1; ROADMAP.md, "Switching to the JS5208") | — |
| Whether OK on the ladder wakes the chip from deep sleep, and whether the RC inputs do (with the §2.5 pull-downs) without spurious wakes | The two wake sources of §4.5 | — |
| `DYNMODEL` 4 vs 8 on track | Whether the automotive filter lags the hardest-accelerating cars (§5.2) | — |
| Whether the LM2940CT-5.0 holds 5.0 V on the MP9e's 5.90 V BEC | 5.9 V is under the 6.25 V from which TI guarantees it; its dropout, 0.2 V at most at 100 mA, should leave room (§2.7) | — |
| Whether a BAT43 clamp keeps a 5 V receiver's pulse under 3.6 V on the pin | The RC inputs' protection of §2.5, worked out at about 3.5 V; to measure with a 5 V source through the 10 kΩ | — |
| The current the device draws from the 5V pad, idle and at its peaks (card writes, radio) | It sets the LM2940's dropout and its heat on a high-voltage BEC; 200 mA is an estimate (§2.7) | — |

## Per-car measurements

The device moves between cars, so the values that depend on the car are recorded per car rather than
once. The receiver's logic level must be within what the front end of §2.5 accepts, and the BEC
voltage within the regulator's input range (§2.7). Note any car whose ESC sends telemetry back over
the throttle wire, since those bursts must be filtered out of the throttle trace (§4.3).

The logic level, widths and frame period come from `tools/rc-scope` (ROADMAP.md step 1.5): signal
wire through 10 kΩ to D3, 10 kΩ from D3 to GND, receiver − to GND, receiver + not connected. Its
readings are about 50 µs apart, so its widths are good to that, and to a few µs averaged.

| Car | Receiver | Channel logic level | BEC voltage | Throttle-wire telemetry | Notes |
| --- | --- | --- | --- | --- | --- |
| Kyosho Inferno MP9e Evo V2 (34111, electric 1/8 buggy) | Kyosho Syncro **KR-331**, 3 channels, with the KT-331P transmitter | **3.3 V** push-pull: high 3.25–3.27 V, low 0.00 V (CH3). Positive pulses, one every **16.6 ms (60 Hz)**, the same on every channel. **Steering (CH1): neutral 1,500 µs, full left 1,050 µs, full right 1,940 µs**, and **throttle (CH2): neutral 1,500 µs, full brake 1,005 µs, full throttle ≈ 1,940 µs** (held under 2 s, so estimated from the peaks), with the KT-331P's trims and end points as they are; CH3 1,000 µs in one position of its switch, the other not yet measured | **5.90 V** (Brainz 8 ESC; multimeter, red to black at the receiver, ESC on) | **None seen**: on CH2, with the ESC armed and driven, the frames stay 16.6 ms apart with no extra edges | Ports, top to bottom: VCC, CH3, CH2, CH1. VCC is the power input, not a channel: its pins carry the rail and no pulses. CH1 steering servo, CH2 the ESC, which powers the receiver through it; **CH3 and VCC are free**. CH3 is the KT-331P's two-position **3ch switch** on the grip (instruction manual p. 10; REV-3C and EPA-3C in its menu), which nothing on this car uses: a candidate for the aux-channel remote trigger (PLAN-ESP32.md §8). The KR-331 has no telemetry and no data port: each port is signal, + and −, and carries only the PWM of its channel |

## Wiring

The KiCad schematics in [`hardware/`](../hardware/README.md):

- [`bench.kicad_sch`](../hardware/bench.pdf) — **the bench build, as wired on 2026-10-19**: the
  display on I²C, the two-button ladder on D0, the GM10 on UART1, and the receiver's CH1 and CH2
  through 10 kΩ to D3 and D2, ground common, the receiver's + rail off the XIAO.
- [`car.kicad_sch`](../hardware/car.pdf) — **in the car, planned**: the BEC → bulk capacitors →
  LM2940CT-5.0 with its 47 µF tantalum → 5V pad path, no diodes, the 1S backup LiPo on the BAT pads
  (§2.7), each RC input through 10 kΩ with a BAT43 clamp to 3V3 and 100 kΩ to ground (§2.5), the
  bench's two keys (§3.1), and the IMU sharing the I²C bus with the display (§2.4, §2.6).

Drawings of where each wire goes, in Italian, for building:
[`wiring-bench.png`](../hardware/wiring-bench.png), [`wiring-car.png`](../hardware/wiring-car.png),
and the car's power path pin by pin, [`wiring-power-ldo.png`](../hardware/wiring-power-ldo.png).
