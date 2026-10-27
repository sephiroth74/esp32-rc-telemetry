#!/usr/bin/env python3
"""Writes the car build's assembly guide, in Italian: assembly/assembly-car.html, then the PDF.

The guide goes from the printed parts and the loose components to the box on the car: the brass
inserts, the perfboard soldered hole by hole in a buildable order, the lid with its parts, the wires
between the two, closing the box and wiring the receiver. The perfboard drawings come from
gen_perfboard.py's data, so they follow the layout it checks; the 3D pictures in assembly/img are
renders of enclosure-car.solidcraft, and two of them get numbered markers placed by projecting the
model's coordinates.

Usage: gen_assembly.py [--pdf], from hardware/. With --pdf it also prints assembly-car.pdf through
headless Google Chrome.
"""
import html
import os
import subprocess
import sys

import gen_perfboard as pb

OUT_DIR = "assembly"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

# ---------------------------------------------------------------- perfboard drawings

S = pb.S
MARGIN_X, MARGIN_TOP, MARGIN_BOTTOM = 70, 170, 170
VW, VH = pb.VIEW_W, pb.VIEW_H
SVG_W, SVG_H = int(VW + 2 * MARGIN_X), int(VH + MARGIN_TOP + MARGIN_BOTTOM)
LABEL_H, TIER, TIERS = 26, 33, 4  # a label's height, the step between two rows of labels, the rows

FILLS = {"U1": "#2e7d32", "U6": "#333", "J1": "#c62828", "J2": "#1e88e5", "C1": "#455a64", "C3": "#7b1fa2",
         "R3": "#d7b98e", "R4": "#d7b98e", "R5": "#d7b98e", "R6": "#d7b98e", "R1": "#d7b98e", "R2": "#d7b98e",
         "SW1": "#9e9e9e", "SW2": "#9e9e9e", "C2": "#f5d76e", "D1": "#ff8a65", "D2": "#ff8a65"}
NAMES = {"U1": "XIAO", "U6": "LM2940", "J1": "J1", "J2": "J2", "C1": "C1", "C2": "C2",
         "C3": "C3", "R3": "R3", "R4": "R4",
         "SW1": "SW1 OK", "SW2": "SW2 NAV", "D1": "D1", "D2": "D2"}
# Where a part's name goes when its body's middle is taken (mm, board coordinates).
NAME_AT = {"U6": (14.4, 4.0), "C3": (18.9, 7.5), "R5": (-2.2, -8.9), "R6": (2.4, -4.6), "R2": (7.6, -11.4),
           "R1": (12.6, -13.97), "C2": (1.15, 16.2), "D2": (-15.4, 2.8)}
# What each lead's hole is for, where it matters for the way round.
LEAD_NOTE = {
    ("D2", "F3"): "anodo", ("D2", "E3"): "catodo (fascia)",
    ("D1", "J9"): "anodo", ("D1", "J13"): "catodo (fascia)",
    ("U6", "A15"): "IN", ("U6", "A16"): "GND", ("U6", "A17"): "OUT",
    ("C3", "A17"): "+ C3", ("C3", "A16"): "− C3",
    ("C1", "D12"): "+", ("C1", "D13"): "−",
    ("C2", "A8"): "GND", ("C2", "A11"): "BEC+",
    ("J1", "B12"): "BEC+", ("J1", "B13"): "GND", ("J2", "B9"): "CH1", ("J2", "B10"): "CH2",
    ("R5", "H9"): "R5 corpo", ("R5", "H8"): "R5", ("R6", "G10"): "R6 corpo", ("R6", "G11"): "R6",
    ("R2", "I12"): "R2 corpo", ("R2", "I11"): "R2", ("R1", "J15"): "R1 corpo", ("R1", "J16"): "R1",
}
# The XIAO has fourteen leads: name only the ends of its two rows.
LABEL_ONLY = {"U1": {"G1": "D0", "G7": "D6", "C1": "5V", "C7": "D7"}}
BY_REF = {p["ref"]: p for p in pb.PARTS}


def esc(s):
    return html.escape(s, quote=True)


class Drawing(pb.Svg):
    def markup(self, title):
        body = "\n".join(self.items)
        return (f'<svg class="board" viewBox="0 0 {SVG_W} {SVG_H}" xmlns="http://www.w3.org/2000/svg" '
                f'role="img" aria-label="{esc(title)}">'
                '<style>text { font-family: Helvetica, Arial, sans-serif; }</style>'
                f'<rect width="{SVG_W}" height="{SVG_H}" fill="#fff"/>{body}</svg>')


def with_owned(refs):
    """The parts named, and the parts that belong to them (the XIAO's headers, the switches' stems)."""
    return [p for p in pb.PARTS if p["ref"] in refs or p.get("owner") in refs]


def part_body(svg, v, p, current):
    ref = p.get("owner") or p["ref"]
    if current:
        fill, stroke, op = FILLS.get(ref, "#bbb"), "#222", 0.85 if ref not in ("U1", "U6", "C3") else 0.6
    else:
        fill, stroke, op = "#d5d5d5", "#9a9a9a", 0.75
    v.shape(svg, p["body"], fill, stroke, 2, opacity=op, dash="5 3" if p["ref"] == "D2" else None)
    if p["ref"] in ("D1", "D2"):
        _, x0, y0, x1, y1 = p["body"]
        k = [h for h, n in p["leads"].items() if n == "+3V3"][0]
        kx, ky = pb.hole_xy(k)
        if x1 - x0 > y1 - y0:
            bx = x1 - 0.9 if kx > x1 else x0 + 0.3
            v.shape(svg, ("rect", bx, y0, bx + 0.6, y1), "#111" if current else "#888", "none", 0)
        else:
            by = y1 - 0.9 if ky > y1 else y0 + 0.3
            v.shape(svg, ("rect", x0, by, x1, by + 0.6), "#111" if current else "#888", "none", 0)
    if p["ref"].startswith("R") and p["ref"] not in ("R3", "R4"):
        a, b = list(p["leads"])
        svg.line([v.hole(a), v.hole(b)], "#8d6e63" if current else "#aaa", 3)


def label_width(text):
    return 12 + 8.6 * len(text)


def label_box(svg, cx, cy, text):
    w = label_width(text)
    svg.rect(cx - w / 2, cy - LABEL_H / 2, w, LABEL_H, "#fff", "#222", 1.5, rx=4)
    svg.text(cx, cy + 6, text, 15, bold=True, anchor="middle")


def place_labels(svg, v, labels):
    """Each hole's label outside the board, with a leader: rows A-E above it, F-J below.

    Labels in a band are spread so that no two overlap: each takes the first of four rows where its
    place is free, and slides along the last row if none is, without leaving the drawing."""
    top, bottom = v.oy, v.oy + VH
    bands = {"top": [], "bottom": []}
    for x, y, text, hole in labels:
        bands["top" if hole[0] in "ABCDE" else "bottom"].append((x, y, text))
    for band, items in bands.items():
        rows = [[] for _ in range(TIERS)]
        for x, y, text in sorted(items):
            w = label_width(text)
            lo, hi = w / 2 + 4, SVG_W - w / 2 - 4
            for k, taken in enumerate(rows):
                cx = min(max(x, lo), hi)
                if all(abs(cx - ox) >= (w + ow) / 2 + 8 for ox, ow in taken):
                    break
            else:
                k, taken = TIERS - 1, rows[TIERS - 1]
                cx = min(max([ox + (ow + w) / 2 + 8 for ox, ow in taken] + [x]), hi)
            taken.append((cx, w))
            cy = top - 24 - TIER * k if band == "top" else bottom + 24 + TIER * k
            svg.line([(x, y), (cx, cy)], "#222", 1.4)
            svg.circle(x, y, 0.32 * S, "#222", "none", 0)
            label_box(svg, cx, cy, text)


def board_drawing(now, done, title):
    svg = Drawing()
    v = pb.View(MARGIN_X, MARGIN_TOP, mirror=False)
    v.board(svg, copper=False)
    for p in with_owned(done):
        part_body(svg, v, p, current=False)
    current = with_owned(now)
    for p in current:
        part_body(svg, v, p, current=True)
    notes = {}
    for p in current:
        for h, net in p["leads"].items():
            x, y = v.hole(h)
            svg.circle(x, y, 0.6 * S, pb.NET_COLOR.get(net, "#fff"), "#000", 2)
            if p["ref"] in LABEL_ONLY:
                if h in LABEL_ONLY[p["ref"]]:
                    notes.setdefault(h, []).append(LABEL_ONLY[p["ref"]][h])
                continue
            notes.setdefault(h, [])
            if (p["ref"], h) in LEAD_NOTE:
                notes[h].append(LEAD_NOTE[(p["ref"], h)])
    for ref in now:
        if ref in NAMES and ref in BY_REF:
            if ref in NAME_AT:
                x, y = v.px(NAME_AT[ref][0]), v.py(NAME_AT[ref][1])
            else:
                x, y = v.centre(BY_REF[ref]["body"])
            dark = ref in ("U1", "U6", "C1", "C3")
            svg.text(x, y + 6, NAMES[ref], 17, bold=True, color="#fff" if dark else "#111", anchor="middle")
    place_labels(svg, v, [(*v.hole(h), " · ".join([h] + n), h) for h, n in notes.items()])
    orient(svg, copper=False)
    return svg.markup(title)


def copper_drawing(nets, done_nets, title):
    svg = Drawing()
    v = pb.View(MARGIN_X, MARGIN_TOP, mirror=True)
    v.board(svg, copper=True)
    for p in pb.PARTS:
        for h in p["leads"]:
            x, y = v.hole(h)
            svg.circle(x, y, 0.45 * S, "#9e9e9e", "#333", 1)
    for i, (net, holes) in enumerate(pb.COPPER_WIRES):
        if i in done_nets:
            svg.line([v.hole(h) for h in holes], "#b9b9b9", 4)
    labels = {}
    for i, (net, holes) in enumerate(pb.COPPER_WIRES):
        if i not in nets:
            continue
        pts = [v.hole(h) for h in holes]
        svg.line(pts, "#fff", 8)
        svg.line(pts, "#555" if net == "GPS_TX_to_D7" else pb.NET_COLOR[net], 5)
        for (x, y), h in zip(pts, holes):
            svg.circle(x, y, 0.6 * S, pb.NET_COLOR[net], "#fff", 1.5)
            labels[h] = (x, y)
    place_labels(svg, v, [(x, y, h, h) for h, (x, y) in labels.items()])
    orient(svg, copper=True)
    return svg.markup(title)


def offboard_drawing(title):
    svg = Drawing()
    v = pb.View(MARGIN_X, MARGIN_TOP, mirror=False)
    v.board(svg, copper=False)
    for p in pb.PARTS:
        part_body(svg, v, p, current=False)
    labels = []
    for label, h, net, side, line in pb.OFF_BOARD:
        x, y = v.hole(h)
        svg.circle(x, y, 0.85 * S, "none", pb.NET_COLOR[net], 5)
        labels.append((x, y, f"{label}: {h}", h))
    place_labels(svg, v, labels)
    orient(svg, copper=False)
    return svg.markup(title)


def orient(svg, copper):
    """Which way round the board is: the box's back on top, the USB side left (right on the copper side)."""
    svg.text(SVG_W / 2, 20, "RETRO · ingresso cavi", 14, bold=True, color="#777", anchor="middle")
    svg.text(SVG_W / 2, SVG_H - 8, "DAVANTI · antenna", 14, bold=True, color="#777", anchor="middle")
    left, right = ("LATO GM10", "LATO USB") if copper else ("LATO USB", "LATO GM10")
    svg.add(f'<text x="20" y="{SVG_H / 2}" font-size="14" font-weight="bold" fill="#777" text-anchor="middle" '
            f'transform="rotate(-90 20 {SVG_H / 2})">{left}</text>')
    svg.add(f'<text x="{SVG_W - 16}" y="{SVG_H / 2}" font-size="14" font-weight="bold" fill="#777" '
            f'text-anchor="middle" transform="rotate(90 {SVG_W - 16} {SVG_H / 2})">{right}</text>')


# ---------------------------------------------------------------- the 3D pictures, with markers

# The bottom views were rendered from straight below the lid, 600 mm away with a narrow lens: a point
# (x, y, z) of the model lands at 699 + k*x, 550 + k*y pixels, k = 9111 / (z + 582) (the camera's
# distance from it, near things a little larger).
def project(x, y, z):
    k = 9111 / (z + 582)
    return 699 + k * x, 550 + k * y


def marked_picture(src, w, h, markers, title):
    items = [f'<image href="img/{src}" x="0" y="0" width="{w}" height="{h}"/>']
    for n, (x, y, z) in markers:
        px, py = project(x, y, z)
        items.append(f'<circle cx="{px:.0f}" cy="{py:.0f}" r="25" fill="#e53935" stroke="#fff" stroke-width="4"/>'
                     f'<text x="{px:.0f}" y="{py + 10:.0f}" font-size="28" font-weight="bold" fill="#fff" '
                     f'text-anchor="middle" font-family="Helvetica, Arial, sans-serif">{n}</text>')
    return (f'<svg class="photo" viewBox="140 160 1120 780" xmlns="http://www.w3.org/2000/svg" role="img" '
            f'aria-label="{esc(title)}">{"".join(items)}</svg>')


LID_BARE = [  # markers on the lid seen from below, nothing mounted
    ("1", (-22.23, -15.875, 6.55)), ("1", (22.22, -15.875, 6.55)), ("1", (-22.23, 15.875, 6.55)),
    ("2", (-21.8, -11.75, 20.6)), ("2", (1.2, -11.75, 20.6)), ("2", (-21.8, 11.75, 20.6)), ("2", (1.2, 11.75, 20.6)),
    ("3", (23.3, 16.6, 18.1)),
    ("4", (9.35, -7.45, 22.1)), ("4", (18.35, -7.45, 22.1)),
    ("5", (18.8, 5.3, 21.1)), ("6", (24.3, -4.6, 16.9)), ("7", (8.25, 5.85, 18.0)),
    ("8", (0.0, -19.0, 14.0)), ("9", (4.9, 20.6, 15.0)),
]
LID_MOUNTED = [  # the same view with the parts on
    ("1", (-10.3, 1.5, 18.1)), ("2", (-10.3, -12.6, 17.0)), ("3", (14.0, -7.45, 22.1)),
    ("4", (17.0, 2.0, 18.0)), ("5", (18.7, 15.5, 18.0)), ("6", (24.4, 6.0, 17.5)),
    ("7", (23.3, 16.6, 17.5)), ("8", (6.0, -19.0, 14.0)), ("9", (8.25, 5.85, 18.0)),
]


# ---------------------------------------------------------------- text

def colour_table():
    rows = [("+ (BEC+, 5 V, 3V3)", "rosso", "#d62828"), ("− (massa, GND)", "nero", "#222222"),
            ("CH1 sterzo, dalla ricevente", "giallo", "#fdd835"), ("CH2 gas, dalla ricevente", "arancione", "#fb8c00"),
            ("sterzo verso D3", "verde chiaro", "#9ccc65"), ("gas verso D2", "rosa", "#f06292"),
            ("SDA del display", "verde", "#2e7d32"), ("SCL del display", "viola", "#8e24aa"),
            ("D6 → RX del GM10", "azzurro", "#00acc1"), ("TX del GM10 → D7", "bianco", "#b0b0b0"),
            ("tasti (D0)", "marrone", "#795548")]
    out = ['<table class="tight"><tr><th>Filo</th><th colspan="2">Colore</th></tr>']
    for what, name, c in rows:
        out.append(f'<tr><td>{esc(what)}</td><td><span class="swatch" style="background:{c}"></span></td>'
                   f'<td>{esc(name)}</td></tr>')
    out.append("</table>")
    return "".join(out)


def copper_list(indices):
    names = {"RX_RAIL": "BEC+", "+3V3": "3V3", "+5V": "+5 V", "RC_STEER": "sterzo (D3)",
             "RC_THROTTLE": "gas (D2)", "KEYS": "tasti (D0)", "D6_to_GPS_RX": "D6 al GM10"}
    out = ['<table class="tight"><tr><th>Filo</th><th>Colore</th><th>Piazzole, nell\'ordine</th></tr>']
    for i in indices:
        net, holes = pb.COPPER_WIRES[i]
        out.append(f'<tr><td><span class="swatch" style="background:{pb.NET_COLOR[net]}"></span> '
                   f'{esc(names.get(net, net))}</td><td>{esc(pb.COLOR_NAME[net])}</td>'
                   f'<td class="mono">{" — ".join(holes)}</td></tr>')
    out.append("</table>")
    return "".join(out)


POWER_WIRES = [0, 1, 2, 3, 4, 5, 6]
SIGNAL_WIRES = [7, 8, 9, 10]


def figure(markup, caption, cls=""):
    return f'<figure class="{cls}">{markup}<figcaption>{caption}</figcaption></figure>'


def img(src, caption, cls=""):
    return figure(f'<img src="img/{src}" alt="{esc(caption)}">', caption, cls)


def step(n, title, *parts):
    return f'<section class="step"><h3><span class="num">{n}</span>{title}</h3>{"".join(parts)}</section>'


def note(kind, text):
    return f'<div class="note {kind}">{text}</div>'


def build_html():
    s = []
    add = s.append

    # Cover.
    add('<section class="cover">')
    add('<h1>Contenitore RC Telemetry<br><span>guida di montaggio</span></h1>')
    add('<p class="sub">Kyosho MP9e con ricevente KR-331 · prototipo · ottobre 2026</p>')
    add(img("07-chiuso.jpg", "Il contenitore chiuso: display e tasti sulla piastra, lo sportellino arancione "
                             "sul fianco sinistro sopra USB-C e microSD, le quattro alette per le fascette.", "big"))
    add('<p>La guida porta dai pezzi stampati e dai componenti sciolti al contenitore montato sulla macchina: '
        'gli inserti, la millefori saldata foro per foro, la piastra con display, GM10 e tasti, i fili tra le due, '
        'la chiusura e il collegamento alla ricevente. Disegni e misure vengono dal modello '
        '<code>hardware/enclosure-car.solidcraft</code> e da <code>gen_perfboard.py</code>, che controlla la '
        'millefori.</p>')
    add('<ol class="toc"><li>Prima di cominciare</li><li>Pezzi stampati</li><li>Inserti a caldo</li>'
        '<li>La millefori</li><li>Prova dell\'alimentazione e XIAO</li><li>Controlli col tester</li>'
        '<li>La piastra</li><li>Chiusura del contenitore</li><li>Sulla macchina</li><li>Primo avvio</li>'
        '<li>Appendice: disegno completo e schema</li></ol>')
    add('</section>')

    # 1. Before starting.
    add('<h2>1. Prima di cominciare</h2>')
    add(img("01-esploso.jpg", "I pezzi stampati, visti dal retro: in basso la vasca, sopra la piastra con le "
                              "colonnine, i due tasti e la barretta gialla del fermo GM10; a destra lo "
                              "sportellino, che scende nelle guide verticali.", "big"))
    add('<h3>Come leggere la guida</h3><ul>'
        '<li><b>Davanti</b> è il lato dell\'antenna, <b>dietro</b> quello dove entrano i cavi della ricevente, '
        '<b>sinistra</b> il lato di USB-C, microSD e sportellino, <b>destra</b> il lato del GM10.</li>'
        '<li><b>Millefori:</b> righe da A a J, colonne da 1 a 17. Guardando il lato componenti con il retro in '
        'alto, la riga A è in alto e la colonna 1 a sinistra: <b>B12</b> è riga B, colonna 12. Ogni colonna di '
        'cinque fori (A–E e F–J) è una striscia di rame: i cinque fori sono già collegati tra loro.</li>'
        '<li><b>Lato rame:</b> girando la scheda, destra e sinistra si scambiano. I disegni del lato rame sono '
        'già girati.</li>'
        '<li>Nei disegni della millefori i pezzi del passo sono a colori, quelli già montati in grigio.</li></ul>')
    add(note("warn", "<b>Regola d'oro:</b> mai la USB della XIAO collegata mentre la macchina è accesa. Sulla "
                     "scheda non ci sono diodi tra i 5 V della ricevente e quelli della USB."))
    add('<h3>Elettronica</h3><table class="tight">'
        '<tr><th>Rif.</th><th>Pezzo</th><th>Note</th></tr>'
        '<tr><td>—</td><td>Millefori EPLZON 50,8 × 38,1</td><td>tipo breadboard saldabile, 17 colonne</td></tr>'
        '<tr><td>U1</td><td>Seeed XIAO ESP32-S3 Sense</td><td>con le due strisce di pin da 7 e la microSD</td></tr>'
        '<tr><td>U6</td><td>LM2940CT-5.0</td><td>TO-220</td></tr>'
        '<tr><td>C1</td><td>470 µF 16 V elettrolitico EEU-FR1C471, Ø8 × 11,5</td><td>low-ESR</td></tr>'
        '<tr><td>C2</td><td>100 nF ceramico a disco</td><td></td></tr>'
        '<tr><td>C3</td><td>100 µF 16 V elettrolitico EEU-FR1C101, Ø5 × 11</td><td>low-ESR: mai un '
        'ceramico al suo posto</td></tr>'
        '<tr><td>R1–R4</td><td>4 × 10 kΩ</td><td></td></tr>'
        '<tr><td>R5, R6</td><td>2 × 100 kΩ</td><td></td></tr>'
        '<tr><td>D1, D2</td><td>2 × BAT43</td><td>la fascia è il catodo</td></tr>'
        '<tr><td>SW1, SW2</td><td>2 pulsanti 6 × 6 × 13</td><td>OK e NAV</td></tr>'
        '<tr><td>J1, J2</td><td>2 connettori JST-PH 2 poli a L, con spine e cavetti</td><td>alimentazione e segnali</td></tr>'
        '<tr><td>—</td><td>Display OLED 0,96" I²C 128 × 64</td><td>4 pin: GND VCC SCL SDA</td></tr>'
        '<tr><td>—</td><td>GM10 Nano V3.1 con il suo cavetto SH 1.0</td><td></td></tr>'
        '<tr><td>—</td><td>Antenna FPC 2,4 GHz della XIAO, con cavetto U.FL</td><td></td></tr>'
        '</table>')
    add('<h3>Minuteria e materiali</h3><ul>'
        '<li>Viti M2: 3 × M2×8, 4 × M2×6, 1 × M2×4 (va bene anche M2×5).</li>'
        '<li>Inserti M2 lunghi 4 mm: 3 da Ø3,45 e 5 da Ø3,2.</li>'
        '<li>Filo in silicone da 26–28 AWG nei colori della tabella, guaina termorestringente sottile.</li>'
        '<li>Biadesivo in schiuma sottile (1 mm) per il GM10; schiuma EVA o neoprene da 2–3 mm sotto il '
        'contenitore; 4 fascette larghe fino a 4 mm; nastro kapton.</li>'
        '<li>Attrezzi: saldatore con punta fine e punta per inserti, tester, calibro, tronchesino, pinzette, '
        'cacciavite a croce piccolo.</li></ul>')
    add('<h3>Colori dei fili</h3>' + colour_table())

    # 2. Printing.
    add('<h2>2. Pezzi stampati</h2>')
    add('<p>Gli STL sono in <code>hardware/stl/</code>. PLA o PETG, strati da 0,2 mm, almeno 4 perimetri, '
        'nessun supporto. Su Bambu Studio lascia la compensazione del piede d\'elefante a 0,15 mm.</p>')
    add('<table class="tight"><tr><th>Pezzo</th><th>File</th><th>Sul piatto</th></tr>'
        '<tr><td>Vasca</td><td><code>vasca.stl</code></td><td>dritta, fondo sul piatto</td></tr>'
        '<tr><td>Piastra</td><td><code>piastra.stl</code></td><td>capovolta, superficie sul piatto</td></tr>'
        '<tr><td>Sportellino</td><td><code>sportellino.stl</code></td><td>faccia esterna sul piatto</td></tr>'
        '<tr><td>Tasti NAV e OK</td><td><code>tasto-nav.stl</code>, <code>tasto-ok.stl</code></td>'
        '<td>a testa in giù, scritta sul piatto</td></tr>'
        '<tr><td>Fermo GM10</td><td><code>fermo-gm10.stl</code></td><td>in piano</td></tr></table>')
    add('<ul><li>Pulisci i fori dei tasti e le guide dello sportellino da fili e sbavature: i tasti devono '
        'scorrere da soli nei loro fori, lo sportellino nelle guide.</li>'
        '<li>Prova lo sportellino nelle guide e i tasti nei fori prima di montare l\'elettronica: se qualcosa '
        'stringe, una passata di carta abrasiva fine sul pezzo piccolo.</li>'
        '<li><b>Prima di stampare la piastra misura C1</b> col calibro: il suo collare ha il foro del parametro '
        '<code>c1_d</code>, da mettere al diametro misurato più 0,3 mm. Il modello è fatto per il più grosso '
        'ammesso dal datasheet, Ø8,5.</li></ul>')

    # 3. Inserts.
    add('<h2>3. Inserti a caldo</h2>')
    add(figure(marked_picture("02-piastra-sotto-raw.jpg", 1400, 1100, LID_BARE,
                              "La piastra vista da sotto, con i punti degli inserti"),
               "La piastra capovolta, vista da sotto. Il davanti (aletta dell'antenna) è in alto, il lato USB a "
               "sinistra.", "big"))
    add('<ol class="legend">'
        '<li><b>Tre colonnine agli angoli:</b> inserti da <b>Ø3,45</b>, dalla punta della colonnina.</li>'
        '<li><b>Quattro fori del display:</b> inserti da <b>Ø3,2</b>, ma <b>dalla superficie superiore</b> '
        'della piastra (i fori attorno alla finestra). Le viti arrivano da sotto, attraverso il display.</li>'
        '<li><b>Colonnina tonda del fermo GM10:</b> inserto da <b>Ø3,2</b>, dalla punta.</li>'
        '<li>Fori dei tasti.</li><li>Cornice del GM10.</li>'
        '<li>Colonnina anteriore del fermo GM10, con la sede della barretta.</li>'
        '<li>Collare di C1.</li><li>Aletta dell\'antenna (davanti).</li>'
        '<li>Linguetta che chiude l\'uscita dei cavi (dietro).</li></ol>')
    add('<ul><li>Saldatore con la punta per inserti a 200–220 °C per il PLA, 240 °C per il PETG.</li>'
        '<li>Appoggia l\'inserto dritto sul foro e lascialo scendere col suo peso e una spinta leggera, '
        'senza inclinarlo, finché è a filo. Togli la punta in verticale e lascia raffreddare 10 secondi senza '
        'toccare.</li>'
        '<li>Se un inserto entra storto, scaldalo di nuovo e raddrizzalo subito.</li>'
        '<li>Prima gli inserti del display (piastra dritta), poi quelli delle colonnine (piastra capovolta).</li></ul>')
    add(note("tip", "Se non l'hai già fatto, prova la misura dei fori su un ritaglio: i diametri sono i "
                    "parametri <code>ins_d</code>, <code>ins_small_d</code> e <code>ins_depth</code> del modello."))

    # 4. The perfboard.
    add('<h2>4. La millefori</h2>')
    add(img("05-millefori-3d.jpg", "La millefori finita: XIAO a sinistra con la Sense sopra, i due pulsanti "
                                   "davanti a destra, C1 (blu) e C3 (viola, coricato sull'LM2940) dietro, i due "
                                   "connettori PH bianchi sul bordo posteriore.", "big"))
    add('<ul><li>Monta i pezzi dal più basso al più alto: ognuno resta appoggiato sul banco mentre saldi.</li>'
        '<li>Dopo ogni saldatura taglia i piedini sotto la scheda a <b>2 mm al massimo</b>: sotto ci sono solo '
        '3 mm di distanziali.</li>'
        '<li>I fori di uno stesso pezzo cadono su strisce diverse, salvo dove il disegno lo dice: controlla '
        'riga e colonna di ogni piedino prima di saldare.</li></ul>')

    add(step("4.1", "D2, sotto la XIAO",
             figure(board_drawing(["D2"], [], "D2"), "D2 da F3 a E3, a cavallo del canale."),
             '<ul><li>BAT43 coricato, <b>fascia (catodo) verso E3</b>, cioè verso il 3V3.</li>'
             '<li>Va per primo: dopo, la XIAO lo copre.</li>'
             '<li>Tienilo aderente alla scheda: la XIAO gli passa sopra a 2,5 mm.</li></ul>'))
    add(step("4.2", "Pezzi coricati: R3, R4 e D1",
             figure(board_drawing(["R3", "R4", "D1"], ["D2"], "R3 R4 D1"),
                    "R3 e R4 attraversano il canale; D1 sta sul bordo anteriore."),
             '<ul><li><b>R3</b> 10 kΩ da E9 a F9, <b>R4</b> 10 kΩ da E10 a F10.</li>'
             '<li><b>D1</b> BAT43 da J9 a J13, <b>fascia verso J13</b> (3V3).</li></ul>'))
    add(step("4.3", "Resistenze in piedi: R5, R6, R2, R1",
             figure(board_drawing(["R5", "R6", "R2", "R1"], ["D2", "R3", "R4", "D1"], "R5 R6 R2 R1"),
                    "Il foro indicato come «corpo» è quello sotto la resistenza."),
             '<ul><li>Il corpo sta in piedi sul primo foro, il piedino di sopra si piega e scende nel secondo.</li>'
             '<li><b>R5</b> 100 kΩ: corpo in H9, piedino in H8. <b>R6</b> 100 kΩ: corpo in G10, piedino in G11.</li>'
             '<li><b>R2</b> 10 kΩ: corpo in I12, piedino in I11. <b>R1</b> 10 kΩ: corpo in J15, piedino in J16.</li>'
             '<li>Tienile dritte: devono restare sotto i 9,5 mm.</li></ul>'))
    add(step("4.4", "I pulsanti",
             figure(board_drawing(["SW2", "SW1"], ["D2", "R3", "R4", "D1", "R5", "R6", "R2", "R1"], "SW1 SW2"),
                    "SW2 (NAV) centrato su G13, SW1 (OK) su G16."),
             '<ul><li>Piega appena i piedini in modo che entrino in un quadrato di 3 × 3 fori.</li>'
             '<li><b>SW2 NAV</b>: F12, H12, F14, H14. <b>SW1 OK</b>: F15, H15, F17, H17.</li>'
             '<li><b>Prima di saldare, col tester:</b> i due piedini nella stessa colonna (per esempio F12 e H12) '
             'devono essere quelli <b>già collegati dentro il pulsante</b>, a pulsante rilasciato. Se no, gira il '
             'pulsante di 90°: altrimenti risulterebbe sempre premuto.</li>'
             '<li>Il pulsante deve stare dritto e appoggiato: lo stelo entra nel cappuccio del tasto.</li></ul>'))
    add(step("4.5", "J1 e J2, i connettori della ricevente",
             figure(board_drawing(["J1", "J2"], ["D2", "R3", "R4", "D1", "R5", "R6", "R2", "R1", "SW1", "SW2"],
                                  "J1 J2"),
                    "La bocca dei due connettori guarda il bordo posteriore della scheda."),
             '<ul><li>PH a L, passo 2 mm, nei fori da 2,54: allarga appena i due piedini.</li>'
             '<li><b>J1</b> alimentazione: BEC+ in B12, GND in B13. <b>J2</b> segnali: CH1 in B9, CH2 in B10.</li>'
             '<li>Bocca verso il retro, corpo appoggiato alla scheda.</li></ul>'))
    add(step("4.6", "LM2940 e C3",
             figure(board_drawing(["U6", "C3"], ["D2", "R3", "R4", "D1", "R5", "R6", "R2", "R1", "SW1", "SW2",
                                                 "J1", "J2"], "LM2940 C3"),
                    "L'LM2940 coricato verso il davanti, C3 sopra di lui."),
             '<ul><li><b>LM2940</b> coricato, <b>aletta in alto e scritta verso la scheda</b>: IN in A15, GND in A16, '
             'OUT in A17. Piega i tre piedini a 90° a filo del corpo.</li>'
             '<li><b>C3</b> 100 µF coricato sopra l\'LM2940 e saldato ai suoi piedini, sopra la scheda: '
             '<b>+ su OUT (A17), − su GND (A16)</b>.</li>'
             '<li>Lascia un po\' di stagno libero sui piedini OUT e GND: lì arrivano anche i fili 5V e GND del '
             'GM10.</li></ul>'))
    add(step("4.7", "C2 e C1",
             figure(board_drawing(["C2", "C1"], ["D2", "R3", "R4", "D1", "R5", "R6", "R2", "R1", "SW1", "SW2",
                                                 "J1", "J2", "U6", "C3"], "C2 C1"),
                    "C2 scavalca J2 su piedini lunghi; C1 sta in piedi tra J1 e la XIAO."),
             '<ul><li><b>C2</b> 100 nF su piedini lunghi in A8 (GND) e A11 (BEC+), il disco a <b>5,5 mm</b> dalla '
             'scheda, sopra J2, con la faccia lungo la riga.</li>'
             '<li><b>C1</b> 470 µF in piedi: <b>+ in D12</b>, − in D13. Piega i piedini di circa 0,6 mm verso il '
             'davanti e verso la XIAO: così non tocca J1 ed entra nel suo collare.</li></ul>'))
    add(step("4.8", "I fili sul lato rame",
             figure(copper_drawing(POWER_WIRES, [], "fili di alimentazione"),
                    "Prima i fili di alimentazione e massa (scheda girata)."),
             copper_list(POWER_WIRES),
             figure(copper_drawing(SIGNAL_WIRES, POWER_WIRES, "fili dei segnali"),
                    "Poi i fili dei segnali; in grigio quelli già fatti."),
             copper_list(SIGNAL_WIRES),
             '<ul><li>Filo isolato da 28 AWG, tagliato a misura, saldato sulle piazzole nell\'ordine della tabella.</li>'
             '<li>Dove un filo passa per più piazzole, spella solo quei punti, o salda spezzoni separati.</li>'
             '<li>Tieni i fili piatti contro la scheda: sotto ci sono solo 3 mm.</li>'
             '<li>B12 e B13 hanno già i piedini di J1: il filo si salda sulla stessa piazzola.</li></ul>'))

    # 5. Power test and the XIAO.
    add('<h2>5. Prova dell\'alimentazione e XIAO</h2>')
    add(step("5.1", "Prova a 5 V, prima della XIAO",
             '<ul><li>Alimenta J1: un alimentatore da banco a 6 V con limite a 100 mA, oppure la presa VCC della '
             'ricevente con la macchina accesa (vedi il capitolo 9 per la spina).</li>'
             '<li>Col tester: circa <b>6 V tra A15 (IN) e A16</b>, <b>5,0 V tra A17 (OUT) e A16</b>.</li>'
             '<li>Corrente quasi zero. Se qualcosa scalda o il limite interviene, stacca e cerca il corto.</li>'
             '<li>Stacca l\'alimentazione prima di andare avanti.</li></ul>'))
    add(step("5.2", "La XIAO",
             figure(board_drawing(["U1"], ["D2", "R3", "R4", "D1", "R5", "R6", "R2", "R1", "SW1", "SW2", "J1", "J2",
                                           "U6", "C3", "C2", "C1"], "XIAO"),
                    "La XIAO a cavallo del canale, USB-C verso sinistra."),
             '<ul><li>Salda le due strisce di pin alla XIAO, poi infilala nella millefori: <b>D0–D6 nella riga G</b> '
             '(G1–G7), <b>5V, GND, 3V3, D10, D9, D8, D7 nella riga C</b> (C1–C7). USB-C verso il bordo sinistro.</li>'
             '<li>Controlla che D2 sotto non tocchi nulla, poi salda dal lato rame e accorcia i pin.</li>'
             '<li>Inserisci la microSD solo alla fine.</li></ul>'))

    # 6. Checks.
    add('<h2>6. Controlli col tester</h2>')
    add('<p>Scheda senza alimentazione, nulla collegato.</p><ul>'
        '<li><b>Nessun corto:</b> BEC+ (B12) – GND (B13), 5 V (A17) – GND, 3V3 (A3) – GND. Su BEC+ e 5 V la '
        'lettura sale lentamente: sono i condensatori che si caricano.</li>'
        '<li><b>Diodi:</b> col tester sulla prova diodi, da I4 (sterzo) al 3V3 e da J3 (gas) al 3V3 circa 0,2–0,3 V; '
        'al contrario, nessuna conduzione.</li>'
        '<li><b>Pulsanti:</b> tra D0 (G1) e GND, premendo OK la resistenza va a zero, premendo NAV a circa '
        '10 kΩ (R2). Se uno dei due legge zero anche da rilasciato, quel pulsante è montato girato.</li>'
        '<li><b>Continuità</b> di ogni filo del lato rame, da un capo all\'altro.</li></ul>')

    # 7. The lid.
    add('<h2>7. La piastra</h2>')
    add('<p>Lavora con la piastra capovolta sul banco, su un panno, con la superficie superiore in basso.</p>')
    add(figure(marked_picture("04-piastra-mappa-raw.jpg", 1400, 1100, LID_MOUNTED,
                              "La piastra con i pezzi montati, vista da sotto"),
               "La piastra capovolta con tutti i pezzi; il davanti è in alto.", "big"))
    add('<ol class="legend"><li>Display, vetro verso la finestra.</li>'
        '<li>Pin del display, verso il davanti.</li><li>Tasti NAV e OK nei loro fori.</li>'
        '<li>GM10 nella cornice, patch ceramica verso la piastra.</li>'
        '<li>Connettore del GM10, verso il retro.</li><li>Barretta del fermo, sotto lo schermo del GM10.</li>'
        '<li>Vite M2×4 del fermo.</li><li>Antenna FPC sull\'aletta anteriore.</li><li>Collare di C1.</li></ol>')
    add(img("03-piastra-montata.jpg", "La stessa piastra vista di sbieco: si vedono i pin del display, i "
                                      "cappucci dei tasti e la barretta del fermo che entra nella sua sede.", "big"))
    add(step("7.1", "Tasti",
             '<ul><li>Infila NAV (sinistra) e OK (destra) nei fori da sotto: la scritta va verso il banco, la '
             'tesa resta appoggiata sulla piastra e il cappuccio guarda in su.</li>'
             '<li>Devono cadere da soli fino alla tesa. Non servono né colla né viti: li trattengono la tesa e, '
             'dopo, il pulsante sotto.</li></ul>'))
    add(step("7.2", "Antenna FPC",
             '<ul><li>Incollala sulla faccia interna dell\'aletta anteriore (quella verso la scheda), centrata, '
             'con l\'uscita del cavetto verso sinistra.</li>'
             '<li>La parte alta dell\'antenna si piega a 90° e resta piatta sotto la piastra.</li>'
             '<li>Il cavetto U.FL va alla XIAO durante la chiusura (passo 8.2). Se è più lungo di 4 cm, fai '
             'un\'ansa e fermala con nastro kapton lungo l\'aletta.</li></ul>'))
    add(step("7.3", "GM10 e il suo fermo",
             '<ul><li>Infila il cavetto SH 1.0 nel connettore del GM10.</li>'
             '<li>Metti un rettangolo di biadesivo in schiuma sulla patch ceramica e appoggia il GM10 nella '
             'cornice, <b>patch verso la piastra e connettore verso il retro</b>, con il cavetto nell\'incavo.</li>'
             '<li>Infila la punta della barretta gialla nella sede della colonnina anteriore (vicino al tasto OK), '
             'appoggiala sullo schermo del GM10 e avvita il dischetto con la <b>M2×4</b> nell\'inserto della '
             'colonnina tonda. Stringi finché la barretta tocca la colonnina: si flette di un decimo e tiene '
             'fermo il modulo.</li></ul>'))
    add(step("7.4", "Display",
             '<ul><li>Accorcia i 4 pin dell\'header a 2 mm e salda un filo su ciascuno: GND nero, VCC rosso, '
             'SCL viola, SDA verde. Lunghezze nella tabella del passo 8.1.</li>'
             '<li>Appoggia il display con il vetro nella finestra e i pin verso il davanti.</li>'
             '<li>Avvitalo con le 4 <b>M2×6</b> attraverso i suoi fori negli inserti. Stringi poco: dietro c\'è il '
             'vetro.</li></ul>'))

    # 8. Closing.
    add('<h2>8. Chiusura del contenitore</h2>')
    add(step("8.1", "I fili tra millefori e piastra",
             figure(offboard_drawing("fili verso la piastra"),
                    "Dove arrivano sulla millefori i fili del GM10 (in alto) e del display (in basso)."),
             '<table class="tight"><tr><th>Filo</th><th>Arriva in</th><th>Nel modello</th><th>Taglia a</th></tr>'
             '<tr><td>GM10 C (5 V), rosso</td><td>piedino OUT dell\'LM2940 (A17)</td><td>14 mm</td><td>30 mm</td></tr>'
             '<tr><td>GM10 G (GND), nero</td><td>piedino GND dell\'LM2940 (A16)</td><td>12 mm</td><td>30 mm</td></tr>'
             '<tr><td>GM10 R (RX), azzurro</td><td>A14 (D6)</td><td>20 mm</td><td>35 mm</td></tr>'
             '<tr><td>GM10 T (TX), bianco</td><td>B7 (D7)</td><td>39 mm</td><td>55 mm</td></tr>'
             '<tr><td>Display SDA, verde</td><td>H5 (D4)</td><td>10 mm</td><td>16 mm</td></tr>'
             '<tr><td>Display SCL, viola</td><td>H6 (D5)</td><td>10 mm</td><td>16 mm</td></tr>'
             '<tr><td>Display GND, nero</td><td>I8</td><td>10 mm</td><td>16 mm</td></tr>'
             '<tr><td>Display VCC, rosso</td><td>I16 (3V3)</td><td>33 mm</td><td>40 mm</td></tr>'
             '<tr><td>Antenna</td><td>U.FL della XIAO</td><td>37 mm</td><td>—</td></tr></table>',
             '<ul><li>Il GM10 si riconosce dalla posizione dei pin, non dai colori del suo cavetto: visto con la '
             'patch in su e il connettore verso di te, da destra sono <b>C G R T</b>. Segna i fili col nastro.</li>'
             '<li>Il <b>VCC del display va al 3V3</b> (I16), non al 5 V.</li></ul>'))
    add(step("8.2", "Millefori sulla piastra",
             '<ol><li>Con la piastra capovolta, appoggia la millefori dritta (lato componenti in su) dietro la '
             'piastra, i due bordi posteriori vicini: è la «cerniera».</li>'
             '<li>Salda i 4 fili del GM10: C e G sui piedini OUT e GND dell\'LM2940 dal lato componenti, R in A14 '
             'e T in B7.</li>'
             '<li>Inserisci le spine JST-PH in J1 e J2, con i cavetti rivolti verso il retro.</li>'
             '<li>Ribalta la millefori sulla piastra attorno alla cerniera, lato componenti verso la piastra. A metà '
             'corsa, quando il cavetto ci arriva, inserisci la spina dell\'antenna nell\'U.FL della XIAO: premi '
             'dritto con la punta di un cacciavite piatto finché scatta.</li>'
             '<li>Chiudi del tutto: i tre fori agli angoli vanno sulle tre colonnine, C1 entra nel suo collare, gli '
             'steli dei pulsanti nei cappucci dei tasti. Intanto infila i 4 fili del display nei fori H5, H6, I8 e '
             'I16.</li>'
             '<li>Salda i fili del display dal lato rame, che ora guarda in su, e accorciali.</li>'
             '<li>Premi i tasti: devono scattare e tornare su da soli.</li></ol>',
             img("06-modulo.jpg", "Il modulo chiuso visto dal retro a sinistra: la piastra sulle colonnine, sotto "
                                  "la millefori, a sinistra USB-C e microSD; dietro escono i quattro cavetti della "
                                  "ricevente sotto la linguetta.", "big")))
    add(step("8.3", "La vasca",
             '<ul><li>Sempre a modulo capovolto, cala la vasca capovolta sulla millefori. Il perno nell\'angolo '
             'posteriore destro entra nel quarto foro della scheda.</li>'
             '<li>I quattro cavetti della ricevente passano nella fessura a U della parete posteriore: la '
             'linguetta della piastra la chiude e li stringe.</li>'
             '<li>Avvita le 3 <b>M2×8</b> dal fondo, attraverso i distanziali e la scheda, negli inserti delle '
             'colonnine. Stringi con due dita: la plastica non chiede di più.</li></ul>',
             img("08-retro.jpg", "Il retro: i quattro cavetti escono dalla fessura sotto la linguetta della piastra.",
                 "big")))
    add(step("8.4", "Sportellino",
             '<ul><li>Gira il contenitore. Infila lo sportellino dall\'alto nelle due guide del fianco sinistro, '
             'accanto al bordo della piastra, con le scanalature fuori e in alto.</li>'
             '<li>Spingilo giù fino in fondo: lo scatto si sente. Per aprirlo spingilo in su con il pollice sulle '
             'scanalature.</li></ul>',
             img("09-sportellino.jpg", "Lo sportellino sollevato: le guide a coda di rondine ai lati dell'apertura, "
                                       "dietro la presa USB-C e sopra la microSD; l'incavo dello scatto è il forellino "
                                       "sopra l'apertura.", "big")))

    # 9. On the car.
    add('<h2>9. Sulla macchina</h2><ul>'
        '<li><b>Fissaggio:</b> sotto il contenitore metti la schiuma da 2–3 mm. Ogni fascetta passa nell\'asola di '
        'un\'aletta e in due fori nell\'alettone: uno sotto l\'asola e uno appena fuori dall\'aletta.</li>'
        '<li><b>J1, alimentazione:</b> il filo su B12 è il <b>+</b> e va al + della presa VCC libera della '
        'ricevente; quello su B13 è il <b>−</b>. Controlla l\'ordine S / + / − sulla serigrafia della ricevente.</li>'
        '<li><b>J2, segnali:</b> il filo su B9 (CH1) va al segnale S del cavo a Y dello sterzo, quello su B10 (CH2) '
        'al segnale S del cavo a Y del gas. Il cavo a Y lascia servo ed ESC collegati come sempre.</li>'
        '<li>Basta un solo −: dentro la ricevente tutti i − sono già collegati.</li></ul>')
    add(note("warn", "Mai il + della ricevente dritto sul 5 V della XIAO: passa sempre da J1 e dall'LM2940. Mai il "
                     "3V3 della XIAO verso la ricevente."))

    # 10. First start.
    add('<h2>10. Primo avvio</h2><ol>'
        '<li><b>Solo USB, macchina spenta e J1 staccato:</b> il display mostra i controlli di avvio; i tasti '
        'rispondono; il GM10 si accende.</li>'
        '<li>Inserisci la microSD dallo sportellino.</li>'
        '<li><b>USB staccata, macchina accesa:</b> il contenitore si accende dalla ricevente. Nella pagina degli '
        'ingressi della sessione, sterzo e gas devono seguire il radiocomando.</li>'
        '<li>Fuori, con il cielo libero, aspetta il fix del GPS.</li></ol>')
    add('<h3>Se qualcosa non va</h3><table class="tight"><tr><th>Sintomo</th><th>Cosa controllare</th></tr>'
        '<tr><td>Un tasto resta premuto</td><td>Il pulsante è più alto del modello: aumenta il parametro '
        '<code>key_play</code> e ristampa il tasto. Controlla anche il verso del pulsante (passo 4.4).</td></tr>'
        '<tr><td>Il tasto balla o non preme</td><td>Diminuisci <code>key_play</code>; se il cappuccio non abbraccia '
        'lo stelo, cambia <code>key_stem_d</code>.</td></tr>'
        '<tr><td>Il GPS non comunica</td><td>R e T scambiati: R va in A14 (D6), T in B7 (D7).</td></tr>'
        '<tr><td>Sterzo e gas non si leggono</td><td>Massa in comune (J1) e segnali sui fili S dei cavi a Y.</td></tr>'
        '<tr><td>Lo sportellino non scatta</td><td>Sbavature nell\'incavo sopra l\'apertura o nelle guide.</td></tr>'
        '<tr><td>C1 non entra nel collare, o ci balla</td><td>Cambia <code>c1_d</code> (diametro di C1 + 0,3 mm) e '
        'ristampa la piastra; se balla poco, basta un giro di kapton attorno al condensatore.</td></tr>'
        '</table>')

    # Appendix.
    add('<h2 class="follow">Appendice: disegno completo e schema</h2>')
    add('<p>Nelle due pagine seguenti, girate: il disegno di <code>gen_perfboard.py</code>, con tutti i pezzi, i '
        'fili e le note, che è il riferimento se qualcosa in questa guida non torna; poi lo schema di cablaggio in '
        'macchina (<code>wiring-car.png</code>).</p>')
    add('<div class="turned"><img src="../perfboard-car.svg" alt="Disegno completo della millefori" '
        'style="width:250mm; left:177.8mm"></div>')
    add('<div class="turned"><img src="../wiring-car.svg" alt="Schema di cablaggio in macchina" '
        'style="width:247mm; left:181.6mm"></div>')

    return PAGE.replace("{{BODY}}", "\n".join(s))


PAGE = """<!DOCTYPE html>
<html lang="it"><head><meta charset="utf-8"><title>Contenitore RC Telemetry: guida di montaggio</title>
<style>
@page { size: A4; margin: 14mm 14mm 16mm 14mm; }
body { font-family: Helvetica, Arial, sans-serif; font-size: 10.5pt; line-height: 1.38; color: #1d1d1f; }
h1 { font-size: 30pt; margin: 0 0 6pt; line-height: 1.1; }
h1 span { font-size: 20pt; font-weight: normal; color: #555; }
h2 { font-size: 17pt; margin: 0 0 8pt; padding-top: 2pt; border-bottom: 2px solid #e53935; break-before: page; }
h3 { font-size: 12.5pt; margin: 14pt 0 5pt; break-after: avoid; }
.sub { color: #555; font-size: 12pt; margin-top: 0; }
.cover h1 { margin-top: 10mm; }
.toc { columns: 2; font-size: 11pt; }
.step { break-inside: auto; }
.step h3 { display: flex; align-items: center; gap: 8pt; }
.num { background: #e53935; color: #fff; border-radius: 4pt; padding: 1pt 6pt; font-size: 11pt; }
figure { margin: 6pt 0 8pt; break-inside: avoid; text-align: center; }
figure img, figure svg { max-width: 100%; height: auto; }
figure.big img, figure.big svg { width: 100%; }
figure svg.board { width: 100%; border: 1px solid #ddd; border-radius: 6px; }
figure svg.photo { width: 100%; }
/* A landscape drawing turned a quarter, to fill a portrait page. */
.turned { position: relative; width: 182mm; height: 252mm; break-before: page; overflow: hidden; }
.turned img { position: absolute; top: 0; transform-origin: top left; transform: rotate(90deg); }
figcaption { font-size: 9pt; color: #555; margin-top: 3pt; text-align: left; }
table.tight { border-collapse: collapse; margin: 6pt 0 10pt; font-size: 9.5pt; break-inside: avoid; }
table.tight th, table.tight td { border: 1px solid #ccc; padding: 2.5pt 6pt; text-align: left; vertical-align: top; }
table.tight th { background: #f3f3f3; }
.swatch { display: inline-block; width: 22pt; height: 8pt; border-radius: 4pt; vertical-align: middle;
          border: 1px solid #999; }
.mono, code { font-family: Menlo, Consolas, monospace; font-size: 9pt; }
.note { border-radius: 6px; padding: 7pt 10pt; margin: 10pt 0; break-inside: avoid; }
.note.warn { background: #fdecea; border-left: 5px solid #c62828; }
.note.tip { background: #e8f4fd; border-left: 5px solid #1e88e5; }
ol.legend { columns: 2; font-size: 9.5pt; }
ul, ol { padding-left: 16pt; }
li { margin: 2pt 0; }
.pagebreak { break-before: page; }
h2.follow { break-before: auto; margin-top: 24pt; }
</style></head><body>
{{BODY}}
</body></html>
"""


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    pb.check()
    page = build_html()
    path = os.path.join(OUT_DIR, "assembly-car.html")
    with open(path, "w") as f:
        f.write(page)
    print("wrote", path)
    if "--pdf" in sys.argv:
        pdf = os.path.abspath("assembly-car.pdf")
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                        "--print-to-pdf-no-header",
                        "--allow-file-access-from-files", f"--print-to-pdf={pdf}",
                        "file://" + os.path.abspath(path)], check=True, capture_output=True)
        print("wrote", pdf)


if __name__ == "__main__":
    main()
