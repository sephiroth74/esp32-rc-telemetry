#!/usr/bin/env python3
"""Draws perfboard-car.svg: where each part and wire goes on the EPLZON board, in Italian.

The board is a solderable mini breadboard, 50.8 x 38.1 mm: 17 columns of holes at 2.54 mm and two
blocks of five rows, A-E and F-J, either side of a 7.62 mm channel. The five holes of a column in a
block share one copper strip. The layout is car.kicad_sch as the prototype builds it: no IMU and no
backup LiPo, but with the BAT43 clamps D1 and D2. The display, the GM10 and the
FPC antenna sit in the lid and reach the board through wires; the keys stand on the board and press
through the lid by printed caps.

Before drawing, the script checks the layout and stops on the first error: every lead in a free hole,
no two bodies in the same place (the lid's parts included), one net per strip, and the strips joined
by the copper-side wires into exactly the nets of the schematic.

Coordinates are millimetres from the board's centre, +X towards the board's right (the LM2940), +Y
towards row A (the back of the box). Heights are above the board's top face.

Usage: gen_perfboard.py <output directory>, then
  rsvg-convert -w 2400 perfboard-car.svg > perfboard-car.png
"""
import math
import sys

PITCH = 2.54
ROW_Y = {"A": 13.97, "B": 11.43, "C": 8.89, "D": 6.35, "E": 3.81,
         "F": -3.81, "G": -6.35, "H": -8.89, "I": -11.43, "J": -13.97}
BOARD_W, BOARD_H = 50.8, 38.1
BOARD_TOP_Z = 6.55          # the board's top face in the enclosure, above the base's underside
LEAD_R = 0.35               # how far a lead reaches around its hole's centre


def col_x(c):
    return -20.44 + PITCH * (c - 1)


def hole_xy(h):
    return col_x(int(h[1:])), ROW_Y[h[0]]


def strip(h):
    return ("T" if h[0] in "ABCDE" else "B") + h[1:]


# The colour of each net's wires, the same in the SolidCraft model: red for every positive rail, black
# for ground, and a colour of its own for each signal. White draws as grey on the white page.
NET_COLOR = {
    "GND": "#222222", "+5V": "#d62828", "RX_RAIL": "#d62828", "+3V3": "#d62828",
    "CH1_STEER": "#fdd835", "CH2_THROTTLE": "#fb8c00", "RC_STEER": "#9ccc65", "RC_THROTTLE": "#f06292",
    "SDA": "#2e7d32", "SCL": "#8e24aa", "D6_to_GPS_RX": "#00acc1", "GPS_TX_to_D7": "#b0b0b0",
    "KEYS": "#795548", "NAV": "#a1887f",
}
COLOR_NAME = {
    "GND": "nero", "+5V": "rosso", "RX_RAIL": "rosso", "+3V3": "rosso", "CH1_STEER": "giallo",
    "CH2_THROTTLE": "arancione", "RC_STEER": "verde chiaro", "RC_THROTTLE": "rosa", "SDA": "verde",
    "SCL": "viola", "D6_to_GPS_RX": "azzurro", "GPS_TX_to_D7": "bianco", "KEYS": "marrone",
}

# The parts on the board. Each lead names its hole and its net; `shared` marks a lead soldered to a
# pad another part already uses (C2 on the copper side under C1, C3 on the LM2940's own leads).
# A body is a rectangle (x0, y0, x1, y1) or a circle (cx, cy, r), from z0 to z1 above the board.
XIAO_PINS = {"G1": ("D0", "KEYS"), "G2": ("D1", "D1"), "G3": ("D2", "RC_THROTTLE"),
             "G4": ("D3", "RC_STEER"), "G5": ("D4", "SDA"), "G6": ("D5", "SCL"),
             "G7": ("D6", "D6_to_GPS_RX"),
             "C1": ("5V", "+5V"), "C2": ("GND", "GND"), "C3": ("3V3", "+3V3"), "C4": ("D10", "SD_CMD"),
             "C5": ("D9", "SD_DAT0"), "C6": ("D8", "SD_CLK"), "C7": ("D7", "GPS_TX_to_D7")}

PARTS = [
    dict(ref="U1", name="XIAO ESP32-S3 Sense", side="top",
         leads={h: n for h, (_, n) in XIAO_PINS.items()},
         body=("rect", -23.36, -7.62, -2.40, 10.16), z=(2.5, 11.0)),
    # The XIAO's two header strips, 2.5 mm of plastic between its PCB and the board.
    dict(ref="U1 header G", name="", side="top", leads={}, owner="U1",
         body=("rect", -21.71, -7.62, -3.93, -5.08), z=(0, 2.5)),
    dict(ref="U1 header C", name="", side="top", leads={}, owner="U1",
         body=("rect", -21.71, 7.62, -3.93, 10.16), z=(0, 2.5)),
    # The RC inputs' clamps, BAT43 in DO-35, the band (cathode) on the 3V3 side. D2 lies under the
    # XIAO across the channel, between its D2 and 3V3 strips: solder it before the XIAO.
    dict(ref="D2", name="BAT43", side="top", leads={"F3": "RC_THROTTLE", "E3": "+3V3"},
         body=("rect", -16.31, -1.9, -14.41, 1.9), z=(0, 1.9)),
    dict(ref="D1", name="BAT43", side="top", leads={"J9": "RC_STEER", "J13": "+3V3"},
         body=("rect", 3.06, -14.92, 6.86, -13.02), z=(0, 1.9)),
    dict(ref="U6", name="LM2940CT-5.0", side="top",
         leads={"A15": "RX_RAIL", "A16": "GND", "A17": "+5V"},
         body=("rect", 12.56, -2.46, 22.76, 12.47), z=(0.5, 5.07)),
    # The receiver's wires plug into two 2-pole JST-PH side-entry headers (S2B-PH-K: 5.9 wide,
    # 4.8 tall, 1.35 behind the pins and 6.25 in front), mouths towards the back wall. Their 2 mm
    # pins go into 2.54 mm holes splayed a little. J1's pins share their pads with copper wires of
    # the same nets.
    dict(ref="J1", name="PH 2 poli (alimentazione)", side="top", shared=True,
         leads={"B12": "RX_RAIL", "B13": "GND"},
         body=("rect", 5.82, 10.08, 11.72, 17.68), z=(0, 4.8)),
    dict(ref="J2", name="PH 2 poli (segnali)", side="top",
         leads={"B9": "CH1_STEER", "B10": "CH2_THROTTLE"},
         body=("rect", -1.80, 10.08, 4.10, 17.68), z=(0, 4.8)),
    # 8.15 x 13.2 mm, its leads bent so the can clears the display, the LM2940 and J1.
    dict(ref="C1", name="470 µF", side="top", leads={"D12": "RX_RAIL", "D13": "GND"},
         body=("circle", 8.25, 5.85, 4.075), z=(1.0, 14.2)),
    # The ceramic disc stands on long legs over J2, from the GND strip 8 to the BEC+ strip 11, its
    # face along the row.
    dict(ref="C2", name="100 nF", side="top", leads={"A8": "GND", "A11": "RX_RAIL"},
         body=("rect", -1.35, 12.72, 3.65, 15.22), z=(5.45, 10.45)),
    dict(ref="C3", name="100 µF", side="top", leads={"A17": "+5V", "A16": "GND"}, shared=True,
         body=("rect", 16.33, -1.5, 21.53, 10.5), z=(5.07, 10.27)),
    dict(ref="R3", name="10k", side="top", leads={"E9": "CH1_STEER", "F9": "RC_STEER"},
         body=("rect", -1.27, -3.15, 1.03, 3.15), z=(0, 2.5)),
    dict(ref="R4", name="10k", side="top", leads={"E10": "CH2_THROTTLE", "F10": "RC_THROTTLE"},
         body=("rect", 1.27, -3.15, 3.57, 3.15), z=(0, 2.5)),
    # Standing resistors: the body over the first hole, the bent lead down into the second.
    dict(ref="R5", name="100k", side="top", leads={"H9": "RC_STEER", "H8": "GND"},
         body=("circle", col_x(9), ROW_Y["H"], 1.25), z=(0.5, 9.5)),
    dict(ref="R6", name="100k", side="top", leads={"G10": "RC_THROTTLE", "G11": "GND"},
         body=("circle", col_x(10), ROW_Y["G"], 1.25), z=(0.5, 9.5)),
    dict(ref="R2", name="10k", side="top", leads={"I12": "NAV", "I11": "GND"},
         body=("circle", col_x(12), ROW_Y["I"], 1.25), z=(0.5, 9.5)),
    dict(ref="R1", name="10k", side="top", leads={"J15": "KEYS", "J16": "+3V3"},
         body=("circle", col_x(15), ROW_Y["J"], 1.25), z=(0.5, 9.5)),
    # The keys: the user's 6x6x13 switches, legs bent into a 3x3 square of holes, the body centred
    # on the middle one (G13 and G16): a 6x6 body 3.5 mm tall, then the 3.5 mm plunger up to 13 mm.
    dict(ref="SW2", name="NAV", side="top",
         leads={"F12": "NAV", "H12": "NAV", "F14": "KEYS", "H14": "KEYS"},
         body=("rect", 7.04, -9.35, 13.04, -3.35), z=(0, 3.5)),
    dict(ref="SW2 stelo", name="", side="top", leads={}, owner="SW2",
         body=("circle", col_x(13), ROW_Y["G"], 1.75), z=(3.5, 13.0)),
    dict(ref="SW1", name="OK", side="top",
         leads={"F15": "KEYS", "H15": "KEYS", "F17": "GND", "H17": "GND"},
         body=("rect", 14.66, -9.35, 20.66, -3.35), z=(0, 3.5)),
    dict(ref="SW1 stelo", name="", side="top", leads={}, owner="SW1",
         body=("circle", col_x(16), ROW_Y["G"], 1.75), z=(3.5, 13.0)),
]

# What the lid holds over the board, heights above the board as well: the display over the XIAO,
# the GM10 over the LM2940 (Flywoo's model: the board and what hangs under it, then the patch and
# the connector on top), the keys' T caps (a stem on the switch whose flange stops under a guide
# hanging from the lid, and a wide pad above the lid, shifted outwards and steadied by a second pin
# in the lid), the FPC antenna on the lid's front flap with its top folded under the lid, and the
# three posts that screw the board to the lid (6 mm feet round the brass inserts, 4 mm above).
LID = [
    dict(ref="OLED", body=("rect", -24.5, -13.7, 3.9, 13.7), z=(18.1 - BOARD_TOP_Z, 20.6 - BOARD_TOP_Z)),
    dict(ref="OLED (vetro e flat)", body=("rect", -23.6, -9.7, 3.0, 13.7), z=(20.6 - BOARD_TOP_Z, 22.4 - BOARD_TOP_Z)),
    dict(ref="GM10 (scheda)", body=("rect", 12.7, -3.46, 24.92, 14.0), z=(17.96 - BOARD_TOP_Z, 19.92 - BOARD_TOP_Z)),
    dict(ref="GM10 (patch)", body=("rect", 12.72, -1.7, 24.69, 14.0), z=(19.92 - BOARD_TOP_Z, 22.1 - BOARD_TOP_Z)),
    dict(ref="tasto NAV (flangia)", body=("circle", 10.04, -6.35, 2.7), z=(13.0, 20.15 - BOARD_TOP_Z)),
    dict(ref="tasto NAV (guida)", body=("circle", 10.04, -6.35, 3.0), z=(20.3 - BOARD_TOP_Z, 22.1 - BOARD_TOP_Z)),
    dict(ref="tasto NAV (perno)", body=("circle", 5.04, -6.35, 1.2), z=(21.5 - BOARD_TOP_Z, 22.1 - BOARD_TOP_Z)),
    dict(ref="tasto OK (flangia)", body=("circle", 17.66, -6.35, 2.7), z=(13.0, 20.15 - BOARD_TOP_Z)),
    dict(ref="tasto OK (guida)", body=("circle", 17.66, -6.35, 3.0), z=(20.3 - BOARD_TOP_Z, 22.1 - BOARD_TOP_Z)),
    dict(ref="tasto OK (perno)", body=("circle", 22.66, -6.35, 1.2), z=(21.5 - BOARD_TOP_Z, 22.1 - BOARD_TOP_Z)),
    dict(ref="aletta e antenna FPC", body=("rect", -19.0, -19.05, 19.0, -18.45), z=(7.0 - BOARD_TOP_Z, 22.1 - BOARD_TOP_Z)),
    dict(ref="antenna FPC (piega)", body=("rect", -18.7, -18.45, 18.7, -16.15), z=(21.9 - BOARD_TOP_Z, 22.1 - BOARD_TOP_Z)),
] + [dict(ref=f"colonnina {k}", body=("circle", x, y, r), z=z)
     for k, x, y in (("FL", -22.23, -15.875), ("FR", 22.22, -15.875), ("BL", -22.23, 15.875))
     for r, z in ((3.0, (0, 5.1)), (2.0, (5.1, 22.1 - BOARD_TOP_Z)))]

# Insulated wires on the copper side, each soldered to the pads it lists, in order.
COPPER_WIRES = [
    ("+5V", ["A17", "B1"]),
    ("GND", ["B2", "B8", "B13", "B16"]),
    ("GND", ["E8", "F8"]),
    ("GND", ["H8", "I11", "G17"]),
    ("RX_RAIL", ["C11", "B12", "C15"]),
    ("+3V3", ["A3", "G16"]),
    ("+3V3", ["J16", "J13"]),
    ("KEYS", ["H1", "G14", "G15"]),
    ("RC_STEER", ["I4", "I9"]),
    ("RC_THROTTLE", ["J3", "J10"]),
    ("D6_to_GPS_RX", ["H7", "B14"]),
]

# Wires leaving the board for the lid's parts, from the component side: the GM10's four from its
# connector, which faces the back (C G R T from the right, seen from above with the patch up and the
# connector toward you, so C is the pin nearest the board's middle), the display's four from
# the front, under its header. GM10 5V and GND are soldered to the LM2940's OUT and GND leads, right
# under the connector. The last two fields place the label: above or below the board, and on which
# of three staggered lines.
OFF_BOARD = [
    ("GM10 TX", "B7", "GPS_TX_to_D7", "top", 0), ("GM10 RX", "A14", "D6_to_GPS_RX", "top", 0),
    ("GM10 GND", "A16", "GND", "top", 1), ("GM10 5V", "A17", "+5V", "top", 0),
    ("OLED SDA", "H5", "SDA", "bottom", 0), ("OLED SCL", "H6", "SCL", "bottom", 1),
    ("OLED GND", "I8", "GND", "bottom", 0), ("OLED VCC", "I16", "+3V3", "bottom", 0),
]

# The schematic's nets for the prototype, as car.kicad_sch draws them without U5, BT1, D1 and D2:
# each must come out as one connected set of strips, and no two of them may touch.
SCHEMATIC_NETS = ["GND", "+5V", "RX_RAIL", "+3V3", "KEYS", "NAV", "CH1_STEER", "CH2_THROTTLE",
                  "RC_STEER", "RC_THROTTLE", "SDA", "SCL", "D6_to_GPS_RX", "GPS_TX_to_D7"]


class LayoutError(Exception):
    pass


def shape_contains(shape, x, y, pad=0.0):
    if shape[0] == "rect":
        _, x0, y0, x1, y1 = shape
        return x0 - pad < x < x1 + pad and y0 - pad < y < y1 + pad
    _, cx, cy, r = shape
    return math.hypot(x - cx, y - cy) < r + pad


def shapes_overlap(a, b):
    if a[0] == "rect" and b[0] == "rect":
        return a[1] < b[3] and b[1] < a[3] and a[2] < b[4] and b[2] < a[4]
    if a[0] == "circle" and b[0] == "circle":
        return math.hypot(a[1] - b[1], a[2] - b[2]) < a[3] + b[3]
    rect, circ = (a, b) if a[0] == "rect" else (b, a)
    _, x0, y0, x1, y1 = rect
    _, cx, cy, r = circ
    nx, ny = min(max(cx, x0), x1), min(max(cy, y0), y1)
    return math.hypot(cx - nx, cy - ny) < r


def check():
    """Raise LayoutError on the first problem; return the net of each strip."""
    used = {}
    for p in PARTS:
        for h in p["leads"]:
            if h[0] not in ROW_Y or not 1 <= int(h[1:]) <= 17:
                raise LayoutError(f"{p['ref']}: {h} is not a hole of the board")
            if h in used and not p.get("shared"):
                raise LayoutError(f"{p['ref']} and {used[h]} both put a lead in {h}")
            used.setdefault(h, p["ref"])
    # A lead may not go down through another part's body on the top side.
    for p in PARTS:
        if p.get("shared"):
            continue
        for h in p["leads"]:
            x, y = hole_xy(h)
            for q in PARTS:
                if q is not p and q["side"] == "top" and not q.get("shared") and q.get("owner") != p["ref"] \
                        and q["z"][0] < p["z"][1] and shape_contains(q["body"], x, y, LEAD_R):
                    raise LayoutError(f"{p['ref']}'s lead in {h} is under {q['ref']}")
    # No two bodies in the same space, the lid's parts included.
    solids = [p for p in PARTS] + LID
    for i, a in enumerate(solids):
        for b in solids[i + 1:]:
            if a.get("side", "top") != b.get("side", "top"):
                continue
            if a["z"][0] < b["z"][1] and b["z"][0] < a["z"][1] and shapes_overlap(a["body"], b["body"]):
                if {a["ref"], b["ref"]} == {"C3", "U6"}:
                    continue
                raise LayoutError(f"{a['ref']} and {b['ref']} take the same space")
    # A copper-side wire may not be soldered on a pad that a copper-side part covers.
    for net, holes in COPPER_WIRES:
        for h in holes:
            x, y = hole_xy(h)
            for q in PARTS:
                if q["side"] == "bottom" and h not in q["leads"] and shape_contains(q["body"], x, y, 0.5):
                    raise LayoutError(f"copper wire {net}: pad {h} is under {q['ref']}")
    # One net per strip.
    strip_net = {}

    def put(h, net, who):
        s = strip(h)
        if strip_net.setdefault(s, net) != net:
            raise LayoutError(f"strip {s} would carry {strip_net[s]} and {net} ({who}, {h})")

    for p in PARTS:
        for h, n in p["leads"].items():
            put(h, n, p["ref"])
    for net, holes in COPPER_WIRES:
        for h in holes:
            put(h, net, f"copper wire {net}")
    for label, h, net, _, _ in OFF_BOARD:
        put(h, net, label)
    # The copper wires must join each net's strips into one, and nothing else.
    parent = {s: s for s in strip_net}

    def find(s):
        while parent[s] != s:
            parent[s] = parent[parent[s]]
            s = parent[s]
        return s

    for net, holes in COPPER_WIRES:
        for a, b in zip(holes, holes[1:]):
            parent[find(strip(a))] = find(strip(b))
    for net in SCHEMATIC_NETS:
        groups = {find(s) for s, n in strip_net.items() if n == net}
        if len(groups) != 1:
            raise LayoutError(f"net {net} is split in {len(groups)} parts: "
                              f"{sorted(s for s, n in strip_net.items() if n == net)}")
    for s, n in strip_net.items():
        for t, m in strip_net.items():
            if n != m and find(s) == find(t):
                raise LayoutError(f"{n} and {m} are joined ({s}, {t})")
    return strip_net


# ---------------------------------------------------------------- drawing

S = 15                      # pixels per millimetre
VIEW_W, VIEW_H = BOARD_W * S, BOARD_H * S
MARGIN, GAP, TOP = 70, 150, 190
W = int(2 * VIEW_W + GAP + 2 * MARGIN)
H = 1290


class Svg:
    def __init__(self):
        self.items = []

    def add(self, s):
        self.items.append(s)

    def text(self, x, y, s, size=13, bold=False, color="#222", anchor="start", italic=False):
        attrs = ' font-weight="bold"' if bold else ""
        attrs += ' font-style="italic"' if italic else ""
        s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{color}" '
                 f'text-anchor="{anchor}"{attrs}>{s}</text>')

    def rect(self, x, y, w, h, fill, stroke="#333", width=2, rx=0, opacity=1.0, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx:.1f}" '
                 f'fill="{fill}" fill-opacity="{opacity}" stroke="{stroke}" stroke-width="{width}"{d}/>')

    def circle(self, x, y, r, fill, stroke="#333", width=2, opacity=1.0, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r:.1f}" fill="{fill}" fill-opacity="{opacity}" '
                 f'stroke="{stroke}" stroke-width="{width}"{d}/>')

    def line(self, pts, color, width=3, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        self.add(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{width}" '
                 f'stroke-linecap="round" stroke-linejoin="round"{d}/>')

    def save(self, path):
        with open(path, "w") as f:
            f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
                    f'viewBox="0 0 {W} {H}">\n'
                    '  <style>text { font-family: Helvetica, Arial, sans-serif; }</style>\n'
                    f'  <rect width="{W}" height="{H}" fill="#fff"/>\n')
            for item in self.items:
                f.write("  " + item + "\n")
            f.write("</svg>\n")


class View:
    """One face of the board: the component side as seen from above, or the copper side turned over."""

    def __init__(self, ox, oy, mirror):
        self.ox, self.oy, self.mirror = ox, oy, mirror

    def px(self, x):
        return self.ox + ((BOARD_W / 2 - x) if self.mirror else (x + BOARD_W / 2)) * S

    def py(self, y):
        return self.oy + (BOARD_H / 2 - y) * S

    def hole(self, h):
        x, y = hole_xy(h)
        return self.px(x), self.py(y)

    def shape(self, svg, shape, fill, stroke="#333", width=2, opacity=1.0, dash=None):
        if shape[0] == "rect":
            _, x0, y0, x1, y1 = shape
            xa, xb = sorted((self.px(x0), self.px(x1)))
            svg.rect(xa, self.py(y1), xb - xa, (y1 - y0) * S, fill, stroke, width, rx=3, opacity=opacity,
                     dash=dash)
        else:
            _, cx, cy, r = shape
            svg.circle(self.px(cx), self.py(cy), r * S, fill, stroke, width, opacity=opacity, dash=dash)

    def centre(self, shape):
        if shape[0] == "rect":
            return self.px((shape[1] + shape[3]) / 2), self.py((shape[2] + shape[4]) / 2)
        return self.px(shape[1]), self.py(shape[2])

    def board(self, svg, copper):
        svg.rect(self.px(-BOARD_W / 2) if not self.mirror else self.px(BOARD_W / 2), self.oy,
                 VIEW_W, VIEW_H, "#f4f1e6" if not copper else "#e8eef0", "#555", 2, rx=3 * S)
        for x, y in ((-22.225, 15.875), (22.225, 15.875), (-22.225, -15.875), (22.225, -15.875)):
            svg.circle(self.px(x), self.py(y), 1.1 * S, "#fff", "#888", 1.5)
        for x in (-20.5, 20.3):
            svg.circle(self.px(x), self.py(0), 1.6 * S, "#fff", "#888", 1.5)
        for c in range(1, 18):
            for block, rows in (("T", "ABCDE"), ("B", "FGHIJ")):
                x = col_x(c)
                y0, y1 = ROW_Y[rows[0]], ROW_Y[rows[-1]]
                if copper:
                    xa = self.px(x) - 1.0 * S
                    svg.rect(xa, self.py(max(y0, y1)) - 1.0 * S, 2.0 * S, (abs(y0 - y1) + 2.0) * S,
                             "#d79a4f", "#b07030", 1, rx=1.0 * S)
                else:
                    svg.line([(self.px(x), self.py(y0)), (self.px(x), self.py(y1))], "#c9c2a8", 4)
        for c in range(1, 18):
            for r in ROW_Y:
                x, y = self.hole(f"{r}{c}")
                if not copper:
                    svg.circle(x, y, 0.95 * S, "#e2c46a", "#b8973a", 1)
                svg.circle(x, y, 0.45 * S, "#fff" if not copper else "#333", "none", 0)
        for c in range(1, 18):
            svg.text(self.px(col_x(c)), self.py(ROW_Y["A"]) - 1.6 * S, str(c), 11, color="#666", anchor="middle")
            svg.text(self.px(col_x(c)), self.py(ROW_Y["J"]) + 2.2 * S, str(c), 11, color="#666", anchor="middle")
        for r, y in ROW_Y.items():
            left, right = self.px(-25.4), self.px(25.4)
            for x in (min(left, right) - 14, max(left, right) + 14):
                svg.text(x, self.py(y) + 4, r, 11, bold=True, color="#666", anchor="middle")


def draw_parts(svg, v):
    fills = {"U1": "#2e7d32", "U6": "#333", "J1": "#c62828", "J2": "#1e88e5", "C1": "#455a64", "C3": "#546e7a",
             "R3": "#d7b98e", "R4": "#d7b98e", "R5": "#d7b98e", "R6": "#d7b98e", "R1": "#d7b98e",
             "R2": "#d7b98e", "SW1": "#9e9e9e", "SW2": "#9e9e9e", "C2": "#f5d76e", "D1": "#ff8a65",
             "D2": "#ff8a65"}
    order = ["U1", "D2", "U6", "C3", "J1", "J2", "C1", "C2", "R3", "R4", "R5", "R6", "R1", "R2", "SW1", "SW2", "D1"]
    by_ref = {p["ref"]: p for p in PARTS}
    for ref in order:
        p = by_ref[ref]
        op = 0.55 if ref in ("U1", "U6", "C3") else 0.85
        v.shape(svg, p["body"], fills[ref], "#222", 2, opacity=op, dash="4 3" if ref == "D2" else None)
        if ref in ("D1", "D2"):
            # The cathode band, at the end towards the 3V3 lead.
            _, x0, y0, x1, y1 = p["body"]
            k = [h for h, n in p["leads"].items() if n == "+3V3"][0]
            kx, ky = hole_xy(k)
            if x1 - x0 > y1 - y0:
                bx = x1 - 0.9 if kx > x1 else x0 + 0.3
                v.shape(svg, ("rect", bx, y0, bx + 0.6, y1), "#111", "#111", 1)
            else:
                by = y1 - 0.9 if ky > y1 else y0 + 0.3
                v.shape(svg, ("rect", x0, by, x1, by + 0.6), "#111", "#111", 1)
        for h, net in p["leads"].items():
            x, y = v.hole(h)
            svg.circle(x, y, 0.55 * S, NET_COLOR.get(net, "#fff"), "#000", 1.5)
        if ref.startswith("R") and ref not in ("R3", "R4"):
            (a, b) = list(p["leads"])
            svg.line([v.hole(a), v.hole(b)], "#8d6e63", 3)
    # Labels, after the bodies so none hides another's.
    lab = {
        "U1": ("XIAO ESP32-S3 Sense", "← USB-C e microSD"),
        "U6": ("U6 LM2940", "aletta in alto"),
        "C3": ("C3 100 µF", "coricato sull'LM2940"),
        "J1": ("J1", ""), "J2": ("J2", ""),
        "C1": ("C1", "470 µF"),
        "SW1": ("SW1 OK", ""), "SW2": ("SW2 NAV", ""),
    }
    for ref, (l1, l2) in lab.items():
        x, y = v.centre(by_ref[ref]["body"])
        dy = {"U1": -42, "U6": -60, "C3": 40, "J1": -82, "J2": -82, "SW1": 4, "SW2": 4, "C1": 0, "C2": 4}.get(ref, 0)
        color = "#fff" if ref in ("U1", "U6", "C1") else "#111"
        svg.text(x, y + dy, l1, 13 if ref != "C1" else 12, bold=True, color=color, anchor="middle")
        if l2:
            svg.text(x, y + dy + 16, l2, 11, color=color, anchor="middle")
    x, y = v.centre(by_ref["C2"]["body"])
    label(svg, x, y - 26, "C2 100n", 10)
    x, y = v.centre(by_ref["D1"]["body"])
    label(svg, x, y + 30, "D1 BAT43", 10)
    x, y = v.centre(by_ref["D2"]["body"])
    label(svg, x + 2, y + 4, "D2 BAT43 (sotto)", 10)
    for ref in ("R3", "R4"):
        x, y = v.centre(by_ref[ref]["body"])
        svg.text(x, y + 4, ref, 10, bold=True, anchor="middle")
    # The standing resistors, each label clear of its neighbours: (hole it sits by, dx, dy, anchor).
    spots = {"R5": ("H9", -22, 4, "end"), "R6": ("G10", 0, -20, "middle"),
             "R2": ("I12", 0, 32, "middle"), "R1": ("J16", 16, 4, "start")}
    for ref, (h, dx, dy, anchor) in spots.items():
        x, y = v.hole(h)
        label(svg, x + dx, y + dy, f"{ref} {by_ref[ref]['name']}", 10, anchor)
    # The XIAO's pads, named.
    for h, (pad, net) in XIAO_PINS.items():
        x, y = v.hole(h)
        svg.text(x, y + (22 if h[0] == "G" else -14), pad, 10, bold=True, color="#fff", anchor="middle")
    for h, name in (("B9", "CH1"), ("B10", "CH2"), ("B12", "BEC+"), ("B13", "GND")):
        x, y = v.hole(h)
        svg.text(x, y - 13, name, 9, bold=True, color="#fff", anchor="middle")
    for h, name in (("A15", "IN"), ("A16", "GND"), ("A17", "OUT")):
        x, y = v.hole(h)
        svg.text(x, y - 13, name, 10, bold=True, color="#111", anchor="middle")


def tag(svg, x, y, text, color):
    w = 10 + 6.6 * len(text)
    svg.rect(x - w / 2, y - 11, w, 18, "#fff", color, 2, rx=4)
    svg.text(x, y + 3, text, 11, bold=True, color="#222", anchor="middle")


def draw_off_board(svg, v):
    """Each wire to the lid: its hole ringed, a leader out past the board's edge, its name there."""
    for label, h, net, side, line in OFF_BOARD:
        x, y = v.hole(h)
        color = NET_COLOR[net]
        ty = v.oy - 22 - 24 * line if side == "top" else v.oy + VIEW_H + 22 + 24 * line
        svg.line([(x, y), (x, ty)], color, 2.5, dash="5 3")
        svg.circle(x, y, 0.8 * S, "none", color, 4)
        tag(svg, x, ty, label, color)


def label(svg, x, y, text, size=10, anchor="middle"):
    """A small label on a white ground, readable over pads and bodies."""
    w = 6 + 0.6 * size * len(text)
    x0 = x - w / 2 if anchor == "middle" else (x if anchor == "start" else x - w)
    svg.rect(x0, y - size, w, size + 5, "#fff", "none", 0, rx=3, opacity=0.85)
    svg.text(x0 + w / 2, y, text, size, bold=True, anchor="middle")


def draw_copper(svg, v, strip_net):
    for net, holes in COPPER_WIRES:
        pts = [v.hole(h) for h in holes]
        svg.line(pts, "#fff", 7)
        svg.line(pts, "#555" if net == "GPS_TX_to_D7" else NET_COLOR[net], 4)
        for x, y in pts:
            svg.circle(x, y, 0.6 * S, NET_COLOR[net], "#fff", 1.5)
    # Every lead's pad, to solder.
    for p in PARTS:
        if p["side"] == "bottom":
            continue
        for h in p["leads"]:
            x, y = v.hole(h)
            svg.circle(x, y, 0.45 * S, "#9e9e9e", "#333", 1)
    # Each strip's net, outside the board beyond its end.
    for s, net in sorted(strip_net.items()):
        if net.startswith("SD_") or net == "D1":
            continue
        short = {"RX_RAIL": "BEC+", "CH1_STEER": "CH1", "CH2_THROTTLE": "CH2", "RC_STEER": "D3",
                 "RC_THROTTLE": "D2", "D6_to_GPS_RX": "D6", "GPS_TX_to_D7": "D7", "+3V3": "3V3",
                 "+5V": "5V"}.get(net, net)
        y = v.oy - 10 if s[0] == "T" else v.oy + VIEW_H + 22
        svg.text(v.px(col_x(int(s[1:]))), y, short, 10, bold=True, color=NET_COLOR.get(net, "#333"),
                 anchor="middle")


def legend(svg, y0):
    x0 = MARGIN
    svg.text(x0, y0, "Fili sul lato rame — filo isolato, saldato sulle piazzole indicate, nell'ordine",
             15, bold=True)
    y = y0 + 26
    for net, holes in COPPER_WIRES:
        svg.add(f'<line x1="{x0}" y1="{y - 4}" x2="{x0 + 34}" y2="{y - 4}" stroke="{NET_COLOR[net]}" '
                f'stroke-width="5" stroke-linecap="round"/>')
        name = {"RX_RAIL": "BEC+ (da J1)", "+3V3": "3V3", "+5V": "+5 V", "RC_STEER": "sterzo (D3)",
                "RC_THROTTLE": "gas (D2)", "KEYS": "tasti (D0)", "D6_to_GPS_RX": "D6 al GM10"}.get(net, net)
        svg.text(x0 + 46, y, f"{name} ({COLOR_NAME[net]}):  " + " — ".join(holes), 13)
        y += 22
    x1 = x0 + 760
    svg.text(x1, y0, "Fili che escono dalla scheda (lato componenti)", 15, bold=True)
    rows = [("Display OLED", "GND → I8,  VCC → I16,  SDA → H5,  SCL → H6"),
            ("GM10", "patch in su, connettore verso di te: da destra C G R T"),
            ("", "5V (C) → piedino OUT dell'LM2940 (A17),  GND (G) → piedino GND (A16),"),
            ("", "RX (R) → A14 (D6),  TX (T) → B7 (D7)"),
            ("Ricevente", "due spine JST-PH da 2 poli, dal retro:"),
            ("", "J1: BEC+ (B12) = + della presa VCC,  GND (B13) = − della presa VCC,"),
            ("", "J2: CH1 (B9) = S del cavo a Y dello sterzo,  CH2 (B10) = S di quello del gas"),
            ("Antenna FPC", "nel connettore U.FL della XIAO")]
    yy = y0 + 26
    for k, t in rows:
        svg.text(x1, yy, k, 13, bold=True)
        svg.text(x1 + 150, yy, t, 13)
        yy += 22
    yy += 14
    svg.text(x1, yy, "Note di montaggio", 15, bold=True)
    notes = [
        "XIAO a cavallo del canale, USB-C verso sinistra: D0–D6 nella fila G, 5V GND 3V3 D10 D9 D8 D7 nella C.",
        "LM2940 coricato, aletta in alto (scritta verso la scheda): IN in A15, GND in A16, OUT in A17.",
        "C3 coricato sopra l'LM2940, saldato ai suoi piedini: + su OUT, − su GND.",
        "J1 e J2 (PH, passo 2 mm) nei fori da 2,54: apri appena i due piedini, la bocca verso il retro.",
        "C2 (disco da 100 nF) su piedini lunghi in A8 e A11, a cavallo di J2: il disco a 5,5 mm dalla scheda.",
        "C1 in piedi in D12 (+) e D13: piega i piedini di circa 0,5 mm verso il davanti e verso la XIAO.",
        "R1, R2, R5, R6 in piedi: il corpo sul primo foro, il piedino piegato nel secondo.",
        "D1 e D2 (BAT43) coricati, la fascia (catodo) verso il 3V3: D1 da J9 a J13,",
        "  D2 sotto la XIAO da F3 a E3: saldalo prima della XIAO.",
        "Tasti: i due piedini nella stessa colonna devono essere quelli già collegati dentro il tasto",
        "  (verifica col tester prima di saldare).",
        "Taglia i piedini sotto la scheda a 2 mm al massimo: sotto ci sono 3 mm di distanziali.",
        "Mai la USB con la macchina accesa (wiring-car): la presa si raggiunge dallo sportellino sul fianco sinistro.",
    ]
    for n in notes:
        yy += 21
        svg.text(x1, yy, n, 12)


def main(out):
    strip_net = check()
    svg = Svg()
    svg.text(MARGIN, 48, "Millefori EPLZON 50,8 × 38,1: montaggio in macchina (prototipo)", 24, bold=True)
    svg.text(MARGIN, 76, "Da hardware/car.kicad_sch senza IMU e LiPo di backup. Ogni colonna di "
             "cinque fori (A–E, F–J) è una striscia di rame: i fori della stessa striscia sono già collegati.",
             14, color="#444")
    svg.text(MARGIN, 98, "Controllato da gen_perfboard.py: piedini in fori liberi, nessun ingombro "
             "sovrapposto (coperchio compreso), una rete per striscia, le reti di car.kicad_sch.",
             13, color="#2e7d32", italic=True)
    top = View(MARGIN, TOP, mirror=False)
    cu = View(MARGIN + VIEW_W + GAP, TOP, mirror=True)
    svg.text(top.ox + VIEW_W / 2, TOP - 56, "Lato componenti (visto dall'alto)", 16, bold=True, anchor="middle")
    svg.text(cu.ox + VIEW_W / 2, TOP - 56, "Lato rame (scheda girata: destra e sinistra scambiate)", 16,
             bold=True, anchor="middle")
    top.board(svg, copper=False)
    draw_parts(svg, top)
    draw_off_board(svg, top)
    cu.board(svg, copper=True)
    draw_copper(svg, cu, strip_net)
    svg.text(top.ox, TOP + VIEW_H + 104, "Retro della scatola (parete con l'ingresso dei cavi) in alto; "
             "parete della microSD a sinistra. Le etichette fuori dalla scheda sono i fili verso il coperchio.",
             12, color="#555", italic=True)
    legend(svg, TOP + VIEW_H + 150)
    svg.save(f"{out}/perfboard-car.svg")
    print("layout ok:", len(PARTS), "parts,", sum(len(h) - 1 for _, h in COPPER_WIRES), "copper wires,",
          len(OFF_BOARD), "wires off the board")


if __name__ == "__main__":
    try:
        main(sys.argv[1] if len(sys.argv) > 1 else ".")
    except LayoutError as e:
        sys.exit(f"layout error: {e}")
