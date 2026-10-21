# Algorithms — gate crossing, s/d projection, cut detection

Implemented twice against the fixtures in [fixtures/](fixtures/): `firmware/lib/geo/` on the device
and `packages/rct_core/` in Dart. Both must agree to the last decimal, because the phone shows a lap
time the desktop app then has to reproduce.

**Status:** the track rules — cell names, recognition, session numbers — are written below and
implemented in `firmware/lib/geo/` (ROADMAP.md step 5.4). The lap algorithms are still to define;
they block on-device lap detection (PLAN-ESP32.md phase 8).

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

## Tracks and sessions on the card

The rules of PLAN-ESP32.md §7.1, to the detail two implementations need to agree on. Positions are
u-blox's integers, degrees × 10⁷, as NAV-PVT gives them; nothing between a fix and a folder name
goes through floating point.

### Cell id

`{N|S}DDdd{E|W}DDDdd`, from the absolute value of each coordinate, **truncated**, never rounded:
degrees, then hundredths of a degree. In integers, for latitude `lat` (× 10⁷):

```text
hemisphere = lat < 0 ? 'S' : 'N'
a          = |lat|
DD         = a / 10⁷              (integer division)
dd         = (a mod 10⁷) / 10⁵
```

and the same for longitude with `E`/`W` and three digits of degrees. Latitude 45.1234, longitude
9.3456 → `N4512E00934`; −0.00001 → `S0000`; −179.99999 → `W17999`.

### Distance

For the radius, between two points a few hundred metres apart, the equirectangular approximation,
in double precision:

```text
φ1, φ2, λ1, λ2   the coordinates in radians: value × 10⁻⁷ × π / 180
Δλ               λ2 − λ1, brought into [−π, π]
x                Δλ × cos((φ1 + φ2) / 2)
y                φ2 − φ1
d                6 371 008.8 m × √(x² + y²)
```

Within 0.1 % of the great circle up to tens of kilometres, far below what matters for a 300 m
radius. Two implementations' `cos` may differ in the last bit, which only matters for a point
exactly on the radius.

### Inside a geo-fence

A fence is a polygon of at least three vertices, in order, implicitly closed. A point is inside by
the even-odd rule, a horizontal ray from the point, counted **in the integer coordinates
themselves** — a horizontal ray's crossings do not change when longitude is scaled by the cosine of
the latitude, so no projection is needed and the test is exact. For each edge from `(xi, yi)` to
`(xj, yj)`, with `x` longitude, `y` latitude and the point at `(x, y)`, all relative to the point:

```text
crosses  = (yi > y) ≠ (yj > y)
and then  (x − xi) × (yj − yi)  <  (y − yi) × (xj − xi)    when yj > yi
          (x − xi) × (yj − yi)  >  (y − yi) × (xj − xi)    when yj < yi
```

in 64-bit integers. A point exactly on an edge may come out either way, as with any such test. A
fence spanning more than 10° of latitude or longitude is invalid and contains nothing: no track is
that size, and it keeps the products within 64 bits.

### Which track

Given the fix — a 3D fix with hAcc ≤ 5 m — and every `track.json` on the card:

1. **By fence.** The tracks whose fence contains the fix. A track with a fence is recognised by its
   fence alone, never by the radius: that is what a fence is for, two clubs a few hundred metres
   apart, only one of them fenced.
2. **Otherwise by radius**, among the tracks without a fence: those whose reference point — the
   start line if one is set, else the centre — lies within `track.match_radius_m` (300 m).
3. Among several of either, the one with the highest `last_used`; on a tie, the first in the order
   the tracks were listed.
4. None: a new track, named as below.

**A new track's name** is the cell id of the fix, if no folder on the card has it; otherwise the
cell id with the first free suffix of `-2` to `-99`. All 99 taken, no new track. **Track › New
track** names its folder the same way, skipping steps 1–3.

### Session numbers

`session_NNN`, numbered per track from the highest existing number plus one, starting at 1. A name
counts only if it is `session_` followed by digits and nothing else. Three digits with leading
zeros, as many as the number needs from 1000 on: `session_999`, then `session_1000`.

## Steering and throttle in percent

A car profile's calibration (PLAN-ESP32.md §3.8) holds three widths for each input, by meaning:
the **negative** end (steering full left, throttle full brake), **neutral**, and the **positive**
end (full right, full throttle). Which of them is the larger number depends on the radio, and does
not matter. Implemented in `firmware/lib/rc/`.

A width `w` becomes a percentage on its own side of neutral, each side with its own scale — an ESC
with 450 µs of travel to full throttle and 200 µs to full brake is half-way at +225 µs and at
−100 µs:

```text
d       = w − neutral
side    = positive  if d and (positive − neutral) have the same sign, else negative
full    = |end of that side − neutral|
p       = (|d| × 100 + ⌊full / 2⌋) ÷ full     (integer division: rounded to the nearest)
percent = min(p, 100), negative on the negative side; 0 when d is 0
```

**A capture is a calibration** when every width is non-zero, each end lies at least 50 µs from
neutral, and the two ends lie on opposite sides of it. Otherwise the summary of Settings ›
Calibrate does not save it: a step was skipped, a stick stayed put, or an end was captured the
wrong way.

**The accepted window** of a calibrated input (§4.3) runs from the smaller end to the larger one,
widened by a margin each way.

