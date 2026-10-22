# Fixtures

Golden files shared by the C++ host tests (`firmware/test/`) and the Dart tests
(`packages/rct_core/test/`). Two implementations of one format only stay in agreement if they are
checked against the same bytes.

Hex fixtures hold one byte per pair of hex digits, 32 a line, with `#` starting a comment; the header
of each says where the bytes came from and what is in them.

| File | What | Used by |
| --- | --- | --- |
| `gm10-indoor-115200.hex` | 1,691 bytes of the GM10's UART as delivered, indoors: NMEA and UBX interleaved, six UBX frames (MON-VER, NAV-PVT, NAV-SAT with no satellite) | `test/test_ubx` (step 3.4) |
| `gm10-fix-230400.hex` | 1,204 bytes of the GM10's UART as check 9 configures it, at a window with a 3D fix: nine NAV-PVT at 10 Hz and the periodic NAV-SAT with 24 satellites, nothing else. **The position is moved** onto the sample point 45.1234560 N, 9.3456780 E, the checksums worked out again; every other byte is the module's | `test/test_ubx` (step 4.2) |
| `rcl-golden-v1.json` | The golden `.rcl` of `docs/protocol.md` as values: every record type, laps twice, the UBX frames from the capture above | `test/test_rcl` (step 5.2); `rct_core` |
| `rcl-golden-v1.hex` | The same, as the 500 bytes of the file. Written by `make_rcl_golden.py` from the JSON — a third implementation, in `struct` and `zlib`, from the document and not from `lib/rcl` — so edit the JSON and run the script, never the hex | `test/test_rcl`; `rct_core` |

What else goes here, as the phases produce it:

- **A recorded lap** with known crossing times, for `lib/geo` (phase 8).

The corrupted variants of the captures are made in the tests from the clean ones, rather than
stored.

**A capture with a fix gives away where it was taken.** Before one goes in, its positions are moved
to the sample point by a constant offset, with the UBX checksums recomputed, and its header says so.

Keep them small enough to read by hand. A fixture nobody can inspect is a fixture nobody trusts.
