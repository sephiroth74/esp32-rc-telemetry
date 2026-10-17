# Fixtures

Golden files shared by the C++ host tests (`firmware/test/`) and the Dart tests
(`packages/rct_core/test/`). Two implementations of one format only stay in agreement if they are
checked against the same bytes.

**Status: empty.** What goes here, as the phases produce it:

- **Captured UBX frames** from the GM10 — NAV-PVT and NAV-SAT, valid and deliberately corrupted —
  for `lib/ubx` (phase 3).
- **A short golden `.rcl`** with every record type, plus the expected decode as JSON, for `lib/rcl`
  and `rct_core` (phase 5).
- **A recorded lap** with known crossing times, for `lib/geo` (phase 8).

Keep them small enough to read by hand. A fixture nobody can inspect is a fixture nobody trusts.
