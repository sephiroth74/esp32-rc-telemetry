# Algorithms — gate crossing, s/d projection, cut detection

Implemented twice against the fixtures in [fixtures/](fixtures/): `firmware/lib/geo/` on the device
and `packages/rct_core/` in Dart. Both must agree to the last decimal, because the phone shows a lap
time the desktop app then has to reproduce.

**Status: not written yet.** Blocks on-device lap detection (PLAN-ESP32.md phase 6).

## To define

- **Gate crossing, interpolated.** At 10 Hz the car travels 1.1–1.7 m between fixes, so a lap timed
  by "the fix nearest the line" is worth ±0.15 s at best. The crossing instant is interpolated
  between the two fixes that straddle the gate; §5.5 sets the honest accuracy expectation.
- **s/d projection** onto a recorded centreline, so two laps can be compared at the same distance
  along the track rather than at the same time.
- **Sector splits** derived from the geometry rather than hand-placed.
- **Cut detection** against the recorded perimeter (`geometry.rcg`).
- **Track cell naming and proximity resolution** — the one piece that exists already, in
  `firmware/lib/geo/geo.h`; the rule it implements is PLAN-ESP32.md §7.1.
