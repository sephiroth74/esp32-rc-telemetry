#!/usr/bin/env python3
"""Writes laps-v1.json: a session at a made-up track, and the laps docs/algorithms.md finds in it.

Two halves, kept apart on purpose:

- The session is simulated: a 10 Hz GNSS trace of a car leaving the pits and lapping a ~325 m
  track, with the troubles of PLAN-ESP32.md §7.5 in its laps — a run off the track, a cut, the line
  missed wide, fixes lost across the line — and a car going back over the line at the end. Seeded,
  so the file comes out the same each time.
- The expected laps are worked out by a third implementation of "Laps" in docs/algorithms.md,
  written from the document and not from lib/geo or rct_core: with a start line, and without one,
  the automatic line and the laps from it.

Run it after changing either, never edit the JSON:  python3 docs/fixtures/make_laps.py
"""
import json, math, os, random

R = 6371008.8
WEEK = 604800000
LAT0, LON0 = 451234560, 93456780  # the sample point of the other fixtures (README.md)
ITOW0 = 302400000                 # Wednesday noon, GPS time
LINE_S = 5.0                      # the desktop line: the main straight's start


# --- The session ---------------------------------------------------------------------------

def catmull_rom(pts, per=60):
    out, n = [], len(pts)
    for i in range(n):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
        for k in range(per):
            t = k / per
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t
                                    + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3)
                             for j in range(2)))
    return out


def resample(poly, step, closed):
    """Points `step` metres apart along `poly`."""
    segs = list(zip(poly, poly[1:] + (poly[:1] if closed else [])))
    out, carry = [poly[0]], 0.0
    for (ax, ay), (bx, by) in segs:
        length = math.hypot(bx - ax, by - ay)
        d = step - carry
        while d <= length:
            out.append((ax + (bx - ax) * d / length, ay + (by - ay) * d / length))
            d += step
        carry = length - (d - step)
    return out


# The centreline, counter-clockwise from the main straight's start, heading east.
SCALE = 1.3  # ~325 m, laps of ~24 s: the loop closes at the first, 20 s on
TRACK = resample(catmull_rom([(x * SCALE, y * SCALE) for x, y in
                              [(0, 0), (30, 0), (60, 0), (76, 7), (80, 24), (68, 36), (50, 31),
                               (34, 40), (18, 50), (-2, 45), (-16, 28), (-14, 9)]]), 0.25, True)
N = len(TRACK)
L = N * 0.25


def at(s):
    i = int((s % L) / 0.25) % N
    return TRACK[i]


def tangent(s):
    (ax, ay), (bx, by) = at(s - 1), at(s + 1)
    n = math.hypot(bx - ax, by - ay)
    return (bx - ax) / n, (by - ay) / n


def curvature(s):
    t1, t2 = tangent(s - 3), tangent(s + 3)
    return abs(t1[0] * t2[1] - t1[1] * t2[0]) / 6


# Speed along the track: what the grip allows in each corner, smoothed, 5 to 16 m/s.
RAW = [min(16.0, max(5.0, math.sqrt(7.0 / max(curvature(i * 0.25), 1e-6)))) for i in range(N)]
SPEED = [sum(RAW[(i + k) % N] for k in range(-40, 41)) / 81 for i in range(N)]


def speed(s):
    return SPEED[int((s % L) / 0.25) % N]


def left_of(s):
    tx, ty = tangent(s)
    return -ty, tx


# What each lap does, by its number: the lap n ends at the line's n-th crossing after the first.
FACTOR = [1.0, 0.97, 1.00, 0.98, 1.01, 0.99, 1.02, 1.00, 1.01, 0.99, 1.03]  # its speed
# The first laps disagree with each other on purpose — off in lap 1, a cut in lap 3 — so that the
# automatic line has to wait for two that agree.
OFF_LAP, CUT_LAP, WIDE_LAP, GAP_LAP, LAST_LAP = 1, 3, 7, 9, 10
OFF = (130.0, 182.0, -90.0)   # from s, to s, metres to the left: out of the track, 90 m right
CUT = (78.0, 244.0)           # straight across the infield, from the end of the main straight
WIDE = 12.0                   # metres left of the line where lap 7 should end, ±25 m around it
PIT, JOIN = (-45.0, 39.0), 280.0


def build_route():
    """The car's path, a point every 0.25 m with the speed it is driven at there, and the points
    where the fixes are lost."""
    route = []
    jx, jy = at(JOIN)
    n = int(math.hypot(jx - PIT[0], jy - PIT[1]) / 0.25)
    route += [(PIT[0] + (jx - PIT[0]) * k / n, PIT[1] + (jy - PIT[1]) * k / n,
               3.0 + 6.0 * k / n) for k in range(n)]
    lap_of = lambda s: int((s - LINE_S) // L)  # 0 until the first crossing
    line = lambda n: (n + 1) * L + LINE_S       # where lap n ends
    s, last = JOIN, line(LAST_LAP)
    gaps = []
    while s < last:
        lap = lap_of(s)
        sm = s % L
        if lap == CUT_LAP and CUT[0] <= sm < CUT[0] + 0.25:
            (ax, ay), (bx, by) = at(CUT[0]), at(CUT[1])
            m = int(math.hypot(bx - ax, by - ay) / 0.25)
            v = min(speed(CUT[0]), speed(CUT[1])) * FACTOR[lap]
            route += [(ax + (bx - ax) * k / m, ay + (by - ay) * k / m, v) for k in range(m)]
            s += CUT[1] - CUT[0]
            continue
        lateral = 0.0
        if lap == OFF_LAP and OFF[0] <= sm <= OFF[1]:
            lateral = OFF[2] * math.sin(math.pi * (sm - OFF[0]) / (OFF[1] - OFF[0]))
        d = abs(s - line(WIDE_LAP))
        if d <= 25.0:
            lateral = WIDE * (1 - d / 25.0)
        (x, y), (lx, ly) = at(s), left_of(s)
        route.append((x + lx * lateral, y + ly * lateral, speed(s) * FACTOR[lap]))
        if abs(s - line(GAP_LAP)) < 0.125:
            gaps.append(len(route) - 1)
        s += 0.25
    # Over the line a last time, 2 m on, back over it in reverse, then on again: all within 5 s.
    for a, b, v in ((last, last + 2.0, 3.0), (last + 2.0, last - 1.0, 3.0), (last - 1.0, last + 30.0, 3.0)):
        (ax, ay), (bx, by) = at(a), at(b)
        m = int(abs(b - a) / 0.25)
        route += [(ax + (bx - ax) * k / m, ay + (by - ay) * k / m, v) for k in range(m)]
    return route, gaps


def simulate(route, gaps):
    rng = random.Random(20261025)
    # Distance along the route, and the epoch at which the car reaches each point.
    epochs, d, i = [], 0.0, 0
    nx = ny = 0.0
    gap_epochs = set()
    cum = [0.0]
    for (ax, ay, _), (bx, by, _) in zip(route, route[1:]):
        cum.append(cum[-1] + math.hypot(bx - ax, by - ay))
    while i < len(route) - 2:
        while i < len(route) - 2 and cum[i + 1] <= d:
            i += 1
        (ax, ay, v), (bx, by, _) = route[i], route[i + 1]
        seg = cum[i + 1] - cum[i]
        f = (d - cum[i]) / seg if seg else 0.0
        x, y = ax + (bx - ax) * f, ay + (by - ay) * f
        heading = math.degrees(math.atan2(bx - ax, by - ay)) % 360
        nx = 0.9 * nx + rng.gauss(0, 0.13)  # GNSS error that wanders, ~0.3 m
        ny = 0.9 * ny + rng.gauss(0, 0.13)
        epochs.append({"x": x + nx, "y": y + ny, "v": v, "h": heading, "i": i})
        d += v * 0.1
    for g in gaps:  # fixes lost 1.5 s around the line
        k = min(range(len(epochs)), key=lambda e: abs(epochs[e]["i"] - g))
        gap_epochs.update(range(k - 7, k + 8))
    out = []
    phi0 = LAT0 * 1e-7 * math.pi / 180
    for k, e in enumerate(epochs):
        lat = LAT0 + round(math.degrees(e["y"] / R) * 1e7)
        lon = LON0 + round(math.degrees(e["x"] / (R * math.cos(phi0))) * 1e7)
        spd = max(0, round((e["v"] + rng.gauss(0, 0.08)) * 1000))
        head = round(((e["h"] + rng.gauss(0, 0.6)) % 360) * 1e5) % 36000000
        itow = (ITOW0 + 100 * k) % WEEK
        tms = 250 + 100 * k + rng.randrange(0, 20)  # the record lands a little after the epoch
        ok = 0 if k in gap_epochs else 1
        out.append([itow, tms, lat, lon, spd, head, ok])
    return out


# --- docs/algorithms.md, "Laps", a third time ------------------------------------------------

def plane(o_lat, o_lon, lat, lon):
    phi0 = o_lat * 1e-7 * math.pi / 180
    lam0 = o_lon * 1e-7 * math.pi / 180
    phi = lat * 1e-7 * math.pi / 180
    lam = lon * 1e-7 * math.pi / 180
    dl = lam - lam0
    while dl > math.pi:
        dl -= 2 * math.pi
    while dl < -math.pi:
        dl += 2 * math.pi
    return R * dl * math.cos(phi0), R * (phi - phi0)


def find_laps(epochs, gate, ends=None):
    """The crossings that count; `ends`, if given, gets the index of the epoch completing each."""
    glat, glon, gh, gw = gate["lat_e7"], gate["lon_e7"], gate["heading_e5"], gate["width_m"]
    h = gh * 1e-5 * math.pi / 180
    sh, ch = math.sin(h), math.cos(h)
    laps, last, prev, acc = [], None, None, 0.0
    for n, e in enumerate(epochs):
        itow, tms, lat, lon, _, _, ok = e
        if not ok:
            continue
        x, y = plane(glat, glon, lat, lon)
        u, v = x * sh + y * ch, x * ch - y * sh
        if prev is None:
            prev = (itow, tms, x, y, u, v)
            continue
        pi_, pt, px, py, pu, pv = prev
        step = math.hypot(x - px, y - py)
        crossed = False
        dt = (itow - pi_) % WEEK
        if dt <= 1000 and pu < 0 <= u:
            f = pu / (pu - u)
            if abs(pv + f * (v - pv)) <= gw / 2:
                tau = pi_ + f * dt
                c = math.floor(tau + 0.5) % WEEK
                if last is None or (c - last["itow"]) % WEEK >= 5000:
                    crossed = True
                    ct = pt + (c - pi_) % WEEK
                    if last is None:
                        laps.append({"lap": 0, "itow": c, "t": ct, "lap_ms": 0, "distance_m": 0.0})
                    else:
                        laps.append({"lap": last["lap"] + 1, "itow": c, "t": ct,
                                     "lap_ms": (c - last["itow"]) % WEEK,
                                     "distance_m": acc + f * step})
                    last = laps[-1]
                    acc = (1 - f) * step
                    if ends is not None:
                        ends.append(n)
        if not crossed:
            acc += step
        prev = (itow, tms, x, y, u, v)
    return laps


def median(values):
    s = sorted(values)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def flags(laps):
    ds = [lap["distance_m"] for lap in laps if lap["lap"] > 0]
    m = median(ds)
    return [("cut" if d < 0.85 * m else "off" if d > 1.30 * m else "") for d in ds]


def point_segment(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    if l2 == 0:
        return math.hypot(p[0] - a[0], p[1] - a[1])
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2))
    return math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy))


def covered(points, path):
    """At least 95 % of `points` within 10 m of the segments of `path`."""
    if len(path) < 2 or not points:
        return False
    near = sum(1 for p in points
               if min(point_segment(p, path[k], path[k + 1]) for k in range(len(path) - 1)) <= 10.0)
    return near * 100 >= 95 * len(points)


def agree(a, da, b, db):
    return min(da, db) >= 0.90 * max(da, db) and covered(a, b) and covered(b, a)


def find_line(epochs):
    trace = [e for e in epochs if e[6]]
    o_lat, o_lon = trace[0][2], trace[0][3]
    pts = [plane(o_lat, o_lon, e[2], e[3]) for e in trace]
    index = {id(e): n for n, e in enumerate(epochs)}
    # 1. The first loop.
    closed = None
    for j, ej in enumerate(trace):
        if ej[4] < 1000:
            continue
        for i in range(j):
            ei = trace[i]
            if ei[4] < 1000 or (ej[0] - ei[0]) % WEEK < 20000:
                continue
            dist = math.hypot(pts[j][0] - pts[i][0], pts[j][1] - pts[i][1])
            dh = abs(ej[5] - ei[5]) % 36000000
            dh = min(dh, 36000000 - dh)
            if dist <= 10.0 and dh <= 4500000:
                closed = j
                break
        if closed is not None:
            break
    if closed is None:
        return None
    # 2. The provisional gate, and its laps over the whole trace.
    provisional = {"lat_e7": trace[closed][2], "lon_e7": trace[closed][3],
                   "heading_e5": trace[closed][5], "width_m": 15}
    ends = []
    laps = find_laps(trace, provisional, ends)
    # 3. The first two consecutive laps that agree.
    for k in range(2, len(laps)):
        a, b = pts[ends[k - 2]:ends[k - 1]], pts[ends[k - 1]:ends[k]]
        if not agree(a, laps[k - 1]["distance_m"], b, laps[k]["distance_m"]):
            continue
        # 4. Their fastest point, smoothed over the trace up to the epoch completing lap k.
        last = ends[k]
        fastest, top = None, None
        for kk in range(ends[k - 2], ends[k]):
            near = [trace[m][4] for m in range(last + 1)
                    if min((trace[m][0] - trace[kk][0]) % WEEK, (trace[kk][0] - trace[m][0]) % WEEK) <= 500]
            mean = sum(near) / len(near)
            if top is None or mean > top:
                fastest, top = kk, mean
        gate = {"lat_e7": trace[fastest][2], "lon_e7": trace[fastest][3],
                "heading_e5": trace[fastest][5], "width_m": 15}
        return {"closed_at": index[id(trace[closed])], "provisional": provisional,
                "provisional_laps": [round(lap["distance_m"], 1) for lap in laps[1:k + 1]],
                "agreeing": [k - 1, k], "fastest": index[id(trace[fastest])],
                "found_at": index[id(trace[max(last, closed)])], "gate": gate}
    return None


def main():
    route, gaps = build_route()
    epochs = simulate(route, gaps)
    (lx, ly), (tx, ty) = at(LINE_S), tangent(LINE_S)
    phi0 = LAT0 * 1e-7 * math.pi / 180
    gate = {"lat_e7": LAT0 + round(math.degrees(ly / R) * 1e7),
            "lon_e7": LON0 + round(math.degrees(lx / (R * math.cos(phi0))) * 1e7),
            "heading_e5": round((math.degrees(math.atan2(tx, ty)) % 360) * 1e5), "width_m": 15}
    laps = find_laps(epochs, gate)
    auto = find_line(epochs)
    auto["laps"] = find_laps(epochs, auto["gate"])
    auto["flags"] = flags(auto["laps"])
    lines = lambda items, indent: (",\n" + indent).join(json.dumps(x) for x in items)
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "laps-v1.json")
    with open(path, "w") as f:
        f.write("{\n")
        f.write('  "about": "Made by make_laps.py: a simulated session and its laps per '
                'docs/algorithms.md. Do not edit; run the script.",\n')
        f.write(f'  "track_length_m": {round(L, 1)},\n')
        f.write(f'  "gate": {json.dumps(gate)},\n')
        f.write('  "laps": [\n    ' + lines(laps, "    ") + "\n  ],\n")
        f.write(f'  "flags": {json.dumps(flags(laps))},\n')
        f.write('  "auto": {\n')
        for k in ("closed_at", "provisional", "provisional_laps", "agreeing", "fastest", "found_at",
                  "gate"):
            f.write(f'    "{k}": {json.dumps(auto[k])},\n')
        f.write('    "laps": [\n      ' + lines(auto["laps"], "      ") + "\n    ],\n")
        f.write(f'    "flags": {json.dumps(auto["flags"])}\n  }},\n')
        f.write('  "epochs_columns": ["itow_ms", "t_ms", "lat_e7", "lon_e7", "speed_mm_s", '
                '"heading_e5", "takes_part"],\n')
        f.write('  "epochs": [\n    ' + lines(epochs, "    ") + "\n  ]\n}\n")
    print(f"{len(epochs)} epochs, track {L:.1f} m")
    for lap in laps:
        print("  line ", lap)
    print("  flags", flags(laps))
    print("  auto  closed at", auto["closed_at"], "provisional laps", auto["provisional_laps"],
          "agreeing", auto["agreeing"], "fastest", auto["fastest"], "found at", auto["found_at"],
          auto["gate"])
    for lap in auto["laps"]:
        print("  auto ", lap)
    print("  flags", auto["flags"])


main()
