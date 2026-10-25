# Algorithms — gate crossing, s/d projection, cut detection

Implemented twice against the fixtures in [fixtures/](fixtures/): `firmware/lib/geo/` on the device
and `packages/rct_core/` in Dart. Both must agree to the last decimal, because the phone shows a lap
time the desktop app then has to reproduce.

**Status:** the track rules — cell names, recognition, session numbers — are written below and
implemented in `firmware/lib/geo/` (ROADMAP.md step 5.4). So are the laps, with a start line and
without one: the crossing, the lap's distance, its flags, and the automatic line (steps 8.1–8.3).
Still to define: the s/d projection, sectors and the geometry.

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

- **s/d projection** onto a recorded centreline, so two laps can be compared at the same distance
  along the track rather than at the same time.
- **Sector splits** derived from the geometry rather than hand-placed.
- **Cut detection against `geometry.rcg`**, where there is one; against the median lap distance it
  is below.
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

## Laps

PLAN-ESP32.md §7.5, to the detail two implementations need to agree on. Implemented in
`firmware/lib/geo/laps.h`, checked against `fixtures/laps-v1.json`.

### The epochs that count

The session's NAV-PVT epochs, in order. One **takes part** when it has `gnssFixOK` and a 3D fix
(`fixType` 3); every other is passed over, as if it had not come. From each: `iTOW` (ms), latitude
and longitude (degrees × 10⁷), `gSpeed` (mm/s), `headMot` (degrees × 10⁵, clockwise from north),
and the session time `t` (ms) of its record.

Times of week wrap at 604 800 000 ms. Every difference of two `iTOW`s below is taken modulo that,
into [0, 604 800 000).

### The local plane

Distances and directions are worked out on a plane around a reference point `O`, in metres:

```text
φ0     the latitude of O in radians: value × 10⁻⁷ × π / 180; λ0 its longitude, the same
Δλ     λ − λ0, brought into [−π, π]
x      6 371 008.8 × Δλ × cos φ0          east
y      6 371 008.8 × (φ − φ0)             north
```

`O` is the gate's centre for the laps, and the first epoch of the trace for the automatic line.
The length of a step from one point to the next is `√(Δx² + Δy²)`.

### The gate

A start line is a gate: a **centre**, a **heading** `h` — the direction of travel across it, as
`headMot` gives it — and a **width** `w`, 15 m unless `start_line.json` says otherwise. On the plane
around its centre, a point `(x, y)` is

```text
u = x sin h + y cos h        ahead of the line, positive in the direction of travel
v = x cos h − y sin h        along it, from its centre, positive to the right
```

### A crossing

Between two consecutive epochs that take part, `A` then `B`:

1. **No more than 1000 ms apart.** Over a longer gap — fixes lost — nothing is crossed: the lap runs
   on to the next crossing, and comes out long.
2. **Forwards:** `u_A < 0 ≤ u_B`. A car going back over the line crosses nothing.
3. **The fraction** of the step at the line: `f = u_A / (u_A − u_B)`, in (0, 1].
4. **Within the gate:** `|v_A + f × (v_B − v_A)| ≤ w / 2`. A crossing outside it does not count: a
   car that misses the start straight completes no lap, and the next lap comes out long.
5. **The moment**, interpolated: `τ = iTOW_A + f × (iTOW_B − iTOW_A)`, in double precision, then
   `⌊τ + 0.5⌋` modulo the week. Its session time is `t_A` plus that moment less `iTOW_A`.
6. **Not within 5000 ms of the last crossing counted**, which is ignored: GNSS noise across the
   line, or a car wobbling over it.

The first crossing counted ends no lap, and is lap `0` in its Lap record. Each later one ends the
next lap, whose time is the difference of the two moments.

### A lap's distance

The length of the path between its two crossings, on the plane around the gate: the sum of the
steps between consecutive epochs that take part, gaps included. The step a crossing falls in is
split at `f`: `f` of its length goes to the lap that ends there, the rest to the next.

### Flags

Against the **median** of the session's lap distances, its laps so far, the lap itself included —
of an even count, the mean of the two in the middle. Under 85 % of it the car skipped part of the
track, and the lap is `cut`; over 130 % it went off, or missed the line, and the lap is `off`. A
flagged lap never becomes the best lap. The median moves as laps come, and so can the flags of
earlier laps: a Lap record keeps the flags known when it was written; the screens and the apps work
them out again.

### Without a start line

The line goes where the car is fastest, once the track is known: **two consecutive laps that
agree** (decided 2026-10-25). A first lap out of the pits, a run off the track or a cut does not
agree with the next, and the line waits for two that do — as many laps as that takes.

The **trace** is every epoch that takes part from the session's start, on the plane around the
first of them. Only those **moving**, with `gSpeed` ≥ 1000 mm/s, enter the loop's search, as
`headMot` means nothing at a standstill.

1. **The first loop.** For each new moving epoch `J`, the candidates are the earlier moving epochs
   `I` with `iTOW_J − iTOW_I` ≥ 20 000 ms, within 10 m of `J`, and a heading within 45° of `J`'s
   (the difference of the two `headMot` brought into [0°, 180°]). The first `J` with a candidate
   closes the loop.
2. **A provisional gate** goes at `J`: its position, its `headMot`, 15 m wide. It only cuts the
   trace into laps: those laps are found as with any line, from the start of the trace, and from
   then on as the epochs come. A lap's **epochs** run from the one that completed the crossing
   starting it to the one before the epoch that completed the crossing ending it.
3. **Two laps agree** when the shorter is at least 90 % of the longer, and at least 95 % of the
   epochs of each lie within 10 m of the other's path — the segments between its consecutive
   epochs, the distance from a point to a segment being to the segment's nearest point. Each time
   a provisional lap ends, it and the one before are compared; the first two that agree decide.
4. **The fastest point** of those two laps: each of their epochs `K` has a smoothed speed, the mean
   `gSpeed` of the trace's epochs whose `iTOW` lies within 500 ms of `K`'s, `K` included, up to the
   epoch that completed the second lap's crossing, the sum taken in whole mm/s before the one
   division. The largest wins, the earliest of equals.
5. **The gate** goes there: `K`'s position as its centre, `K`'s `headMot` as its heading, 15 m wide,
   source `auto`. It is found at the epoch that completed the second lap, or at the loop's closure
   when the two laps were already in the trace then.
6. **The laps are found again** from the start of the trace with it, and from then on as the
   epochs come.

The device keeps the trace of the last 10 minutes: two laps that agree have to come within them,
and the laps found again start at most that far back.

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

**A capture is a calibration** when each input captured — one with no pulse is left out, as a car
with only the throttle tapped (§4.3) — has every width non-zero, each end at least 50 µs from
neutral, and its two ends on opposite sides of it; and at least one input was captured. Otherwise
the summary of Settings › Calibrate does not save it, and says why, the first input first: a step
was missed (`Steering step missed`), a stick stayed put (`not moved`), an end was captured the
wrong way (`wrong way`), or nothing was captured at all.

**The accepted window** of a calibrated input (§4.3) runs from the smaller end to the larger one,
widened by 100 µs each way (`RC_WINDOW_MARGIN_US`). An input left out of the calibration keeps the
800–2200 µs of an uncalibrated one.

