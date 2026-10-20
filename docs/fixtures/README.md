# Fixtures

Golden files shared by the C++ host tests (`firmware/test/`) and the Dart tests
(`packages/rct_core/test/`). Two implementations of one format only stay in agreement if they are
checked against the same bytes.

Hex fixtures hold one byte per pair of hex digits, 32 a line, with `#` starting a comment; the header
of each says where the bytes came from and what is in them.

| File | What | Used by |
| --- | --- | --- |
| `gm10-indoor-115200.hex` | 1,691 bytes of the GM10's UART as delivered, indoors: NMEA and UBX interleaved, six UBX frames (MON-VER, NAV-PVT, NAV-SAT with no satellite) | `test/test_ubx` (step 3.4) |
| `rcl-golden-v1.json` | The golden `.rcl` of `docs/protocol.md` as values: every record type, laps twice, the UBX frames from the capture above | `test/test_rcl` (step 5.2); `rct_core` |
| `rcl-golden-v1.hex` | The same, as the 500 bytes of the file. Written by `make_rcl_golden.py` from the JSON — a third implementation, in `struct` and `zlib`, from the document and not from `lib/rcl` — so edit the JSON and run the script, never the hex | `test/test_rcl`; `rct_core` |

What else goes here, as the phases produce it:

- **Captured UBX frames with a fix** — NAV-PVT with a position, NAV-SAT with satellites — for the
  NAV decode of `lib/ubx` (phase 4). The corrupted variants are made in the tests from the clean
  captures, rather than stored.
- **A recorded lap** with known crossing times, for `lib/geo` (phase 8).

Keep them small enough to read by hand. A fixture nobody can inspect is a fixture nobody trusts.
