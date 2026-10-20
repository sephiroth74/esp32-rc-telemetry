# Protocol — `.rcl` binary format and BLE GATT

**Single source of truth** for the on-disk log format and the BLE service. The firmware
(`firmware/lib/rcl/`) and the Dart core (`packages/rct_core/`) are two implementations of what this
file says, checked against the shared fixtures in [fixtures/](fixtures/).

**Status:** the `.rcl` format is written down (ROADMAP.md step 5.1); its two implementations come
with step 5.2 and the Dart core. The BLE service is still to define (phase 9).

## `.rcl` — the session log

One file per session, `session.rcl` in the session's folder (PLAN-ESP32.md §7.1). It holds the
high-rate stream, everything the device sees while a session is open; what is config-sized — the
car profile and its calibration, the track, the UTC start, the POST in words — goes into the
`meta.json` beside it, as JSON (§7.2).

### What the format has to survive

- **A power cut at any moment.** The file is written in 32 KB chunks with a flush every 5 s, into
  extents pre-allocated 4 MB at a time (§4.4). A session that ends without SAVING leaves a file
  whose last records are cut short, followed by whatever the card held in the pre-allocated space:
  zeros, or the bytes of an older session. **Records are self-delimiting and checked**, so a reader
  takes every whole record and stops at the end of them (§4.5).
- **Nothing thrown away** (§5.3). UBX frames are stored **whole and verbatim**, sync to checksum, for
  every message the module sends, so a field nobody reads today is still there tomorrow, and a
  message added to the configuration needs no format change.
- **Two implementations.** C++ on the device, Dart in the apps. Every field has one width, one byte
  order, one unit, written in the tables below.

### Conventions

- **Little-endian** throughout, as UBX is.
- Integers are `u8`/`u16`/`u32`/`u64` unsigned and `i8`/`i16`/`i32` signed. `char[n]` is ASCII,
  padded with zeros, with no terminator when it fills all `n` bytes.
- **CRC-32** is CRC-32/ISO-HDLC, the one of zlib, PNG and Ethernet: polynomial `0x04C11DB7` reflected,
  initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. The check value of `"123456789"` is
  `0xCBF43926`. `lib/rcl` carries its own, so the device and the host tests run the same code; in
  Dart, `getCrc32` of `package:archive` is this one. A CRC continues from a previous value, which is
  how the nonce below goes in.
- **Session time** `t_ms` is milliseconds since the session began, the moment the file header was
  written, as a `u32`: 49 days before it wraps, far beyond any session. Absolute time comes from the
  GNSS: every NAV-PVT carries its GPS time of week and, once the module knows it, the UTC date and
  time, and the UBX records' `t_ms` maps session time onto it.

### File layout

```text
file header (24 bytes)
record
record
…
session footer record   (absent after a power cut)
```

#### File header

| Offset | Type | Field | Value |
| --- | --- | --- | --- |
| 0 | `u8[8]` | magic | `89 52 43 4C 0D 0A 1A 0A` — `\x89 R C L \r \n \x1A \n`, after PNG's: a byte above 0x7F, then the line endings and the end-of-file mark a text-mode copy would mangle |
| 8 | `u16` | version | `1`. A reader refuses a version it does not know |
| 10 | `u16` | header size | `24`. Where the first record starts; a later version may grow the header, and a reader skips what it does not know |
| 12 | `u64` | nonce | Random, drawn when the session starts (`esp_random`). It seeds every record's CRC |
| 20 | `u32` | CRC | CRC-32 of bytes 0–19 |

#### Record

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u8` | sync | `0xA5` |
| 1 | `u8` | type | The tables below. A reader skips a type it does not know |
| 2 | `u16` | length | Of the payload, 0–65,535 |
| 4 | `u32` | `t_ms` | Session time; what it stands for is each type's |
| 8 | `u8[length]` | payload | |
| 8 + length | `u32` | CRC | CRC-32 over the 8 bytes of the nonce, then bytes 1 to 7 + length of the record: type, length, time and payload |

Twelve bytes around each payload. **The nonce is what makes a stale record fail**: an older
session's record in the pre-allocated space has a valid CRC for *its* nonce only, so it can never be
taken for one of this session's.

#### Reading

1. Check the file header: magic, a known version, its CRC. Records start at `header size`.
2. At each position: a byte other than `0xA5` is not a record. A record whose length runs past the
   end of the file is the cut-short end of the data, and reading stops. A record whose CRC does not
   match is not one either.
3. After anything that is not a record, look for the next `0xA5` one byte further on and try again,
   counting the bytes skipped. That is how a reader crosses a zero-filled or stale tail, and the one
   corrupted record a card might produce mid-file, without giving up the records after it.
4. A file with no session footer ended without SAVING.

`t_ms` never decreases from one record of a type to the next. Across types it can step back a
little: the records come from different tasks through queues of their own, and the logger writes
them in the order they reach it.

**Growing a payload.** A later version may append fields to a payload, never change or remove one.
A reader takes the fields it knows and ignores any bytes after them; a payload shorter than the
fields of its version is invalid. A record type whose layout must change otherwise gets a new type.

### Record types

| Type | Record | When | `t_ms` |
| --- | --- | --- | --- |
| `0x01` | Session header | Once, the first record | 0 |
| `0x02` | UBX frame | Every frame the GNSS module sends | When the frame was received |
| `0x03` | IMU aggregate | Every epoch, 100 ms | Start of the window |
| `0x04` | IMU raw | Every epoch, when raw logging is on (§9, question 7) | The first sample |
| `0x05` | RC samples | Every epoch | The first sample |
| `0x06` | Marker | OK pressed in a session (§3.6), or the aux channel's trigger | The press |
| `0x07` | Lap | A crossing of the start line (phase 8) | When the crossing was found |
| `0x08` | Session footer | Once, the last record, at a graceful close | The close |

Types `0x00` and `0xFF` are never used: they are what an erased card reads as.

#### `0x01` Session header

What a reader needs to interpret the other records, and whose session this is. Everything else
about the session is in `meta.json`.

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u32` | session id | The global, monotonic id from NVS (§7.1): the identity the apps key on |
| 4 | `u8[6]` | device | The ESP32's base MAC address, for telling two devices' sessions apart |
| 10 | `u16` | POST | The failed-check mask of the boot this session ran in (§3.3) |
| 12 | `char[16]` | firmware | `FW_VERSION`, e.g. `0.1.0-dev` |
| 28 | `u16` | epoch | Milliseconds per epoch, `100`: the window of the IMU aggregates and of an RC record |
| 30 | `u16` | RC period | Microseconds between RC samples, `20000` for 50 Hz |
| 32 | `u16` | IMU raw period | Microseconds between raw IMU samples, `10000` for 100 Hz; `0` with raw logging off |
| 34 | `u8` | accelerometer range | Full scale in g: `4`, `8`, `16` or `32`; `0` without an IMU (POST check 7) |
| 35 | `u8` | — | `0` |
| 36 | `u16` | gyroscope range | Full scale in degrees per second, `125` to `2000`; `0` without an IMU |
| 38 | `u8` | car profile | Index of the active profile, 0–7; its name and calibration are in `meta.json` |
| 39 | `u8` | — | `0` |

40 bytes.

#### `0x02` UBX frame

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u8[length]` | frame | The whole frame as the module sent it: `B5 62`, class, id, length, payload, checksum |

The frame carries its class, id and payload, and its own checksum, already checked by the device: it
went through `lib/ubx`'s scanner. Any UBX parser reads it as it is. NAV-PVT at 10 Hz and NAV-SAT at
1 Hz are what the configuration asks for (§5.2); invalid epochs are logged like the rest.

#### `0x03` IMU aggregate

The samples of one epoch window, `t_ms` to `t_ms + epoch`, summed up per axis in the sensor's own
units: one LSB is the range over 32,768. Phase 7.

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u16` | count | Samples in the window; `0` means the fields after it mean nothing |
| 2 | `i16[3]` | ×6 axes | For each of ax, ay, az, gx, gy, gz in that order: minimum, maximum, mean rounded to the nearest |

38 bytes. Axes are the sensor's (§2.6); the mounting is in `meta.json`.

#### `0x04` IMU raw

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u8` | count | Samples, at the header's IMU raw period from `t_ms` |
| 1 | `u8` | — | `0` |
| 2 | `i16[6]` | × count | ax, ay, az, gx, gy, gz |

#### `0x05` RC samples

The receiver's pulse widths, sampled at the header's RC period from `t_ms`. A sample is the width of
the latest valid pulse on that input (§4.3), or `0` when the input has delivered none in the last
100 ms: an absent input or a dead one, never a stale value.

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u8` | count | Samples |
| 1 | `u8` | — | `0` |
| 2 | `u16[3]` | × count | Steering, throttle, aux, in microseconds |

At 50 Hz, five samples and 32 bytes an epoch.

#### `0x06` Marker

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u16` | number | 1 for the session's first, as the toast says it (`Marker 1`) |
| 2 | `u8` | source | `0` the OK key, `1` the aux channel |
| 3 | `u8` | — | `0` |

#### `0x07` Lap

A crossing of the start line, found by the device as the laps of phase 8 do. The desktop app may find
the laps again from the UBX records, better; these are what the device knew on the day.

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u16` | lap | The lap this crossing ends: `0` for the first crossing, which ends none |
| 2 | `u8` | flags | Bit 0 `cut`, bit 1 `off` (§8.3): never the best lap |
| 3 | `u8` | line | Where the start line came from (§7.5): `0` found automatically, `1` set on the device, `2` from the desktop app |
| 4 | `u32` | crossing | Session time of the crossing, interpolated between the epochs either side |
| 8 | `u32` | crossing iTOW | The same moment as GPS time of week, in milliseconds |
| 12 | `u32` | lap time | Milliseconds since the previous crossing; `0` for the first |

#### `0x08` Session footer

Written at a graceful close: stopped by hand, by the automatic stop, or on SAVING after the car was
switched off (§3.6, §4.5). Every count covers the whole session.

| Offset | Type | Field | |
| --- | --- | --- | --- |
| 0 | `u8` | close | `0` by hand, `1` the automatic stop, `2` power off (SAVING), `3` a card error |
| 1 | `u8[3]` | — | `0` |
| 4 | `u32` | UBX frames | Written |
| 8 | `u32` | records | Written, this one excluded |
| 12 | `u32` | UART overruns | The GNSS UART's FIFO or receive buffer overflowed (§5.3) |
| 16 | `u32` | UART errors | Framing, parity, break |
| 20 | `u32` | UBX bad checksums | |
| 24 | `u32` | UBX oversize | Lengths over 1,024 |
| 28 | `u32` | GNSS queue full | Frames the gnss task's queue had no room for |
| 32 | `u32` | ring overflow | Records the logger's PSRAM ring had no room for (§4.4) |
| 36 | `u32` | RC rejects, steering | Pulses outside the accepted window or period (§4.3) |
| 40 | `u32` | RC rejects, throttle | |
| 44 | `u32` | RC rejects, aux | |
| 48 | `u32` | writes | Chunk writes to the card |
| 52 | `u32` | slow writes | Writes over 250 ms (`LOG_SLOW_WRITE_MS`) |
| 56 | `u32` | write max | Longest write, ms |
| 60 | `u32` | write p99 | 99th percentile, ms |
| 64 | `u32` | ring high water | Most bytes ever waiting in the ring |

68 bytes. The same numbers go into `meta.json`, for the apps to show without reading the log.

### Budget

At 10 Hz, per second: NAV-PVT 10 × (100 + 12), NAV-SAT about 400 + 12, IMU aggregates 10 × 50,
RC samples 10 × 44: about **2.5 KB/s**, 9 MB an hour, and 1.3 KB/s more with the raw IMU on. Within
the 3–4 KB/s §4.4 sizes the ring and the extents for.

## BLE GATT

To define: service and characteristic UUIDs, the **status** notification payload, the **control**
command set, and the credit/ACK scheme for **download**. Roles are sketched in PLAN-ESP32.md §7.4.
There is no live stream: BLE runs only in Transfer mode.
