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
| Output as found | NMEA and UBX together |
| Both UART directions | **Work**: the UBX-MON-VER poll is answered |
| UBX-MON-VER | sw `ROM SPG 5.10 (7b202e)`, hw `000A0000`; `FWVER=SPG 5.10`, `PROTVER=34.10`; `GPS;GLO;GAL;BDS`, `SBAS;QZSS` |
| Fix | Not yet — indoors, 0 satellites. To repeat with a view of the sky |

`PROTVER=34.10` confirms the configuration-database interface that §5.2 relies on (`CFG-VALSET`).
The `ROM` prefix says the firmware runs from the chip's ROM. That hints the module carries no flash
of its own, which bears on the config-flash question below but does not settle it; a VALSET to the
FLASH layer will.

### Lesson: solder the headers

Pins pushed through the XIAO's holes without solder make intermittent contact. The I²C lines read
connected on one boot and open on the next, and the display never answered until the headers were
soldered. Every bench result above is from soldered headers.

## Still to measure

| Quantity | Why it matters | Value |
| --- | --- | --- |
| Which GPIO is the microSD DAT3 line — 21 or 3 | Seeed's docs contradict themselves; decides whether D2 stays the throttle input or throttle moves to D1 (§2.2). Session 1 did not settle it: the card's internal pull-up on DAT3 makes SD mode work either way | — |
| Whether this GM10 V3.1 carries a magnetometer, and whether it has config flash | Decides whether VALSET to FLASH is worth attempting (§5.2). Firmware runs from ROM, see above | — |
| Time to first fix, cold and with the backup LiPo | Sets expectations for the GNSS screen (§3.4) and confirms the value of the BBR backup (§2.7) | — |
| Whether the GM10 runs from 3V3 | On battery the 5V pad is dead, so a GM10 on 5 V loses its BBR in SLEEP and every run starts cold (§2.7, §9 question 16) | — |
| JS5208 ladder: the ADC reading of each key, once the switch is fitted | UP, DOWN and RIGHT are still nominal in `KEY_MV_BOUNDS` (§3.1; ROADMAP.md, "Switching to the JS5208") | — |
| Whether OK on the ladder wakes the chip from deep sleep, and whether the RC inputs do (with the §2.5 pull-downs) without spurious wakes | The two wake sources of §4.5 | — |
| `DYNMODEL` 4 vs 8 on track | Whether the automotive filter lags the hardest-accelerating cars (§5.2) | — |

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

To document once built: the BEC → regulator → 5V pad path with the 1S backup LiPo on the BAT pads
(§2.7), the 74LVC2G17 level-shifter for the steering and throttle channels (§2.5), and the I²C bus
shared by the display and the IMU (§2.4, §2.6).
