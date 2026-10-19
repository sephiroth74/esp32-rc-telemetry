# Fixtures

Golden files shared by the C++ host tests (`firmware/test/`) and the Dart tests
(`packages/rct_core/test/`). Two implementations of one format only stay in agreement if they are
checked against the same bytes.

Hex fixtures hold one byte per pair of hex digits, 32 a line, with `#` starting a comment; the header
of each says where the bytes came from and what is in them.

| File | What | Used by |
| --- | --- | --- |
| `gm10-indoor-115200.hex` | 1,691 bytes of the GM10's UART as delivered, indoors: NMEA and UBX interleaved, six UBX frames (MON-VER, NAV-PVT, NAV-SAT with no satellite) | `test/test_ubx` (step 3.4) |

What else goes here, as the phases produce it:

- **Captured UBX frames with a fix** — NAV-PVT with a position, NAV-SAT with satellites — for the
  NAV decode of `lib/ubx` (phase 4). The corrupted variants are made in the tests from the clean
  captures, rather than stored.
- **A short golden `.rcl`** with every record type, plus the expected decode as JSON, for `lib/rcl`
  and `rct_core` (phase 5).
- **A recorded lap** with known crossing times, for `lib/geo` (phase 8).

Keep them small enough to read by hand. A fixture nobody can inspect is a fixture nobody trusts.
