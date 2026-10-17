# Algorithms — gate crossing, s/d projection, cut detection

Implemented twice against the fixtures in [fixtures/](fixtures/): `firmware/lib/geo/` on the device
and `packages/rct_core/` in Dart. Both must agree to the last decimal, because the phone shows a lap
time the desktop app then has to reproduce.

**Status: not written yet.** Blocks on-device lap detection (PLAN-ESP32.md phase 8).

## Fixed already

PLAN-ESP32.md §7.5 settles what these algorithms must do, before how:

- **The start line is optional.** Without one, the gate goes at the fastest point of the first lap
  found by loop closure, and the laps before it are worked out again.
- **The geometry is optional** on the device, and built by the desktop app from every session.
- **Buggies leave the track**: a crossing counts only within the gate's width, two crossings closer
  than 5 s count once, and laps are flagged `cut` or `off` against the median lap distance. Flagged
  laps never become the best lap and never feed the geometry, which is the median path of the rest.
- Every `start_line.json` and `geometry.rcg` carries its source — `auto`, `device` or `desktop` —
  and a better source is never overwritten by a worse one.

## To define

- **Gate crossing, interpolated.** At 10 Hz the car travels 1.1–1.7 m between fixes, so a lap timed
  by "the fix nearest the line" is worth ±0.15 s at best. The crossing instant is interpolated
  between the two fixes that straddle the gate; §5.5 sets the honest accuracy expectation.
- **s/d projection** onto a recorded centreline, so two laps can be compared at the same distance
  along the track rather than at the same time.
- **Sector splits** derived from the geometry rather than hand-placed.
- **Cut detection**: against the median lap distance first (§7.5), and against `geometry.rcg`
  where there is one.
- **Loop closure and the fastest point**, for the automatic start line.
- **The median path**, building `geometry.rcg` from the unflagged laps of every session.
- **Track cell naming and proximity resolution** — the one piece that exists already, in
  `firmware/lib/geo/geo.h`; the rule it implements is PLAN-ESP32.md §7.1.
