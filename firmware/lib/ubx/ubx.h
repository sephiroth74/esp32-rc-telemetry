// UBX frame scanning and decoding for the u-blox M10 — PLAN-ESP32.md §5.4.
//
// Skeleton only. To implement in phase 3: frame sync on 0xB5 0x62, length,
// Fletcher-8 checksum, and NAV-PVT / NAV-SAT decode. The scanner validates and
// hands frames over whole, it never transforms them — the raw payload is what
// gets logged (§5.3).
//
// Framework-free by design: nothing under lib/ may include Arduino.h (§6.1),
// which is what lets this be tested on the host against captured frames in
// docs/fixtures/.
#pragma once
