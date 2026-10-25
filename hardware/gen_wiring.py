#!/usr/bin/env python3
"""Draws wiring-bench.svg and wiring-car.svg: where each wire goes, in Italian, for building.

The KiCad sheets beside them are the reference for the nets; these show the same connections as
the parts look on the bench, the XIAO with its pads where they are. The two drawings share every
part but the power and the RC inputs' protection, which only the car has (car.kicad_sch).

Usage: gen_wiring.py <output directory>, then render each with
  rsvg-convert -w 2550 wiring-car.svg > wiring-car.png
"""
import sys

OUT = sys.argv[1]
W, H = 1700, 1250

POS, NEG, SIG = "#d62828", "#222", "#e0a100"
SDA, SCL, UART, V33 = "#2e7d32", "#8e24aa", "#00897b", "#1f6feb"


class Svg:
    def __init__(self):
        self.items = []

    def add(self, s):
        self.items.append(s)

    def path(self, d, color, width=3):
        self.add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" '
                 'stroke-linecap="round" stroke-linejoin="round"/>')

    def text(self, x, y, s, size=13, bold=False, color="#222", anchor="start", italic=False):
        attrs = f' font-weight="bold"' if bold else ""
        attrs += f' font-style="italic"' if italic else ""
        s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.add(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" text-anchor="{anchor}"{attrs}>{s}</text>')

    def rect(self, x, y, w, h, fill, stroke="#333", width=2, rx=6, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" '
                 f'stroke-width="{width}"{d}/>')

    def dot(self, x, y, color=NEG):
        self.add(f'<circle cx="{x}" cy="{y}" r="4" fill="{color}"/>')

    def cross(self, x, y):
        self.add(f'<g stroke="#666" stroke-width="2.5" stroke-linecap="round"><line x1="{x - 6}" y1="{y - 6}" '
                 f'x2="{x + 6}" y2="{y + 6}"/><line x1="{x - 6}" y1="{y + 6}" x2="{x + 6}" y2="{y - 6}"/></g>')

    def tag(self, x, y, label, color):
        w = 8 + 7 * len(label)
        self.rect(x - w / 2, y - 8, w, 16, color, stroke=color, width=1, rx=3)
        self.text(x, y + 4, label, size=10, bold=True, color="#fff", anchor="middle")

    def gnd(self, x, y):
        self.tag(x, y, "GND", NEG)

    def v33(self, x, y):
        self.tag(x, y, "3V3", V33)

    def resistor_v(self, x, y1, y2, label=None, left=False):
        """A resistor drawn upright between y1 and y2, leads included, its body in the middle."""
        top, bottom = min(y1, y2), max(y1, y2)
        mid = (top + bottom) / 2
        self.path(f"M{x},{top} V{mid - 14} M{x},{mid + 14} V{bottom}", "#333", 2)
        self.rect(x - 6, mid - 14, 12, 28, "#fff", rx=0)
        if label:
            self.text(x - 12 if left else x + 12, mid + 4, label, size=11, anchor="end" if left else "start")

    def resistor_h(self, x1, x2, y, label=None):
        mid = (x1 + x2) / 2
        self.rect(mid - 25, y - 7, 50, 14, "#fff", rx=0)
        if label:
            self.text(mid, y - 12, label, size=11, bold=True, anchor="middle")

    def diode_up(self, x, y_anode, y_cathode, label):
        """A diode pointing up, the anode below: what a clamp to 3V3 is."""
        mid = (y_anode + y_cathode) / 2
        self.path(f"M{x},{y_anode} V{mid + 9} M{x},{mid - 9} V{y_cathode}", "#333", 2)
        self.add(f'<polygon points="{x - 10},{mid + 9} {x + 10},{mid + 9} {x},{mid - 9}" fill="#333"/>')
        self.path(f"M{x - 10},{mid - 9} H{x + 10}", "#333", 3)
        self.text(x + 14, mid + 4, label, size=11)

    def cap(self, x, y_top, y_bottom, label, sub, polar):
        """A capacitor hanging from a rail at y_top to a ground tag under y_bottom."""
        self.path(f"M{x},{y_top} V{y_top + 23}", POS)
        self.path(f"M{x - 12},{y_top + 23} H{x + 12}", "#333", 3)
        if polar:
            self.add(f'<path d="M{x - 12},{y_top + 37} Q{x},{y_top + 29} {x + 12},{y_top + 37}" fill="none" '
                     'stroke="#333" stroke-width="3"/>')
            self.text(x - 19, y_top + 19, "+", size=11, color=POS)
        else:
            self.path(f"M{x - 12},{y_top + 31} H{x + 12}", "#333", 3)
        self.path(f"M{x},{y_top + 32} V{y_bottom}", NEG)
        self.gnd(x, y_bottom + 8)
        self.dot(x, y_top, POS)
        self.text(x + 18, y_top + 28, label, size=11, bold=True)
        self.text(x + 18, y_top + 41, sub, size=10, color="#555")

    def save(self, path):
        body = "\n  ".join(self.items)
        open(path, "w").write(
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">\n'
            '  <style>text { font-family: Helvetica, Arial, sans-serif; }</style>\n'
            f'  <rect width="{W}" height="{H}" fill="#fff"/>\n  {body}\n</svg>\n')


# --- The parts both drawings share ---------------------------------------------------------------

PORTS = {"VCC": 420, "CH3": 480, "CH2": 540, "CH1": 640}  # the + pin of each; - above, S below


def receiver(s, vcc_note):
    s.rect(40, 360, 222, 360, "#e8eef8")
    s.text(56, 386, "Ricevente", 15, bold=True)
    s.text(56, 404, "Kyosho KR-331", 12)
    notes = {"VCC": vcc_note, "CH3": "libero", "CH2": "gas → ESC", "CH1": "sterzo → servo"}
    for name, y in PORTS.items():
        s.text(56, y + 4, name, 13, bold=True)
        s.text(56, y + 19, notes[name], 11, color="#555")
        s.rect(246, y - 18, 16, 36, "#333", rx=2)
        for dy, pin in ((-10, "−"), (0, "+"), (10, "S")):
            s.text(242, y + dy + 3, pin, 9, color="#333", anchor="end")


def y_cable(s, y, device, up):
    """The Y on a port: the device on one branch, above or below, the tap on the other."""
    for dy, color in ((-10, NEG), (0, POS), (10, SIG)):
        s.path(f"M262,{y + dy} H330", color)
    for dx, color in ((8, NEG), (16, POS), (24, SIG)):
        s.path(f"M{330 + dx},{y - 20 if up else y + 20} V{y - 35 if up else y + 50}", color)
    s.rect(330, y - 20, 32, 40, "#555", rx=4)
    s.text(346, y + 5, "Y", 14, bold=True, color="#fff", anchor="middle")
    top = y - 90 if up else y + 50
    s.rect(300, top, 180, 55, "#f3f3f3")
    s.text(390, top + 22, device[0], 13, bold=True, anchor="middle")
    if device[1]:
        s.text(390, top + 40, device[1], 10, anchor="middle")


def taps(s, ground_on_throttle):
    """From each Y: the signal goes on; + is cut; - too, but for the bench's one ground."""
    for name, label in (("CH2", "GAS (CH2)"), ("CH1", "STERZO (CH1)")):
        y = PORTS[name]
        if ground_on_throttle and name == "CH2":
            s.path(f"M362,{y - 10} H470", NEG)
            s.gnd(492, y - 10)
        else:
            s.path(f"M362,{y - 10} H398", NEG)
            s.cross(404, y - 10)
        s.path(f"M362,{y} H414", POS)
        s.cross(420, y)
        s.text(440, y + 30, label, 12, bold=True, color="#9a6b00")


XL, XR = 1014, 1186  # the XIAO's pad columns
LEFT = ["D0", "D1", "D2", "D3", "D4", "D5", "D6"]
RIGHT = ["5V", "GND", "3V3", "D10", "D9", "D8", "D7"]


def pad_y(name):
    return 470 + 40 * (LEFT.index(name) if name in LEFT else RIGHT.index(name))


def xiao(s, used):
    s.rect(1000, 430, 200, 320, "#2d6a4f", stroke="#1b4332", rx=10)
    s.rect(1070, 412, 60, 24, "#c9c9c9", stroke="#555", width=1.5)
    s.text(1100, 428, "USB-C", 10, anchor="middle")
    s.text(1100, 580, "XIAO", 17, bold=True, color="#fff", anchor="middle")
    s.text(1100, 598, "ESP32-S3", 12, color="#fff", anchor="middle")
    s.text(1100, 614, "Sense", 12, color="#fff", anchor="middle")
    for names, x, tx, anchor in ((LEFT, XL, 1030, "start"), (RIGHT, XR, 1170, "end")):
        for name in names:
            y = pad_y(name)
            s.add(f'<circle cx="{x}" cy="{y}" r="8" fill="#d4a017" stroke="#7a5c00" stroke-width="1.5"/>')
            on = name in used
            s.text(tx, y + 4, name, 12, bold=on, color="#fff" if on else "#a8c8b8", anchor=anchor)
    s.path(f"M{XR},{pad_y('GND')} H1215", NEG)
    s.gnd(1236, pad_y("GND"))
    s.path(f"M{XR},{pad_y('3V3')} H1215", V33)
    s.v33(1236, pad_y("3V3"))


def keys(s):
    """The two buttons on D0's ladder (§3.1), drawn above the line from D0."""
    y = pad_y("D0")
    s.path(f"M{XL},{y} H760", "#333", 2)
    s.dot(900, y, "#333")
    s.dot(840, y, "#333")
    s.resistor_v(900, y, 415, "R1 10 kΩ")
    s.v33(900, 407)
    for x, name, top in ((840, "OK", 420), (780, "NAV", 422)):
        s.path(f"M{x},{y} V455 M{x},430 V{top}", "#333", 2)
        s.rect(x - 18, 430, 36, 25, "#fff4d6", rx=4)
        s.text(x, 447, name, 11, bold=True, anchor="middle")
    s.gnd(840, 412)
    s.resistor_v(780, 422, 384, "R2 10 kΩ", left=True)
    s.gnd(780, 376)
    s.text(752, y + 4, "tasti su D0", 11, color="#555", anchor="end")


def display(s):
    """The OLED on I²C: VCC on 3V3, SDA to D4, SCL to D5."""
    s.rect(800, 850, 160, 80, "#e3f2fd")
    pins = (("GND", 830), ("VCC", 860), ("SDA", 890), ("SCL", 920))
    for name, x in pins:
        s.rect(x - 4, 842, 8, 8, "#333", rx=1)
        s.text(x, 866, name, 9, bold=True, anchor="middle")
    s.path("M830,842 V820", NEG)
    s.gnd(830, 812)
    s.path("M860,842 V790", V33)
    s.v33(860, 782)
    s.path(f"M890,842 V{pad_y('D4')} H{XL}", SDA)
    s.path(f"M920,842 V{pad_y('D5')} H{XL}", SCL)
    s.text(880, 892, "Display OLED 0,96\"", 12, bold=True, anchor="middle")
    s.text(880, 910, "SSD1306, I²C 0x3C", 10, anchor="middle")
    s.text(880, 946, "ordine dei pin: vedi serigrafia", 10, color="#555", anchor="middle")


def imu(s):
    """The optional IMU (§2.6): on the display's I²C bus, so no pin of its own; drawn dashed."""
    s.rect(360, 830, 260, 100, "#f5f5f5", stroke="#888", width=1.5, dash="6 4")
    for (name, x), (label, color) in zip((("GND", 400), ("VIN", 440), ("SDA", 480), ("SCL", 520)),
                                         (("GND", NEG), ("3V3", V33), ("SDA", SDA), ("SCL", SCL))):
        s.rect(x - 4, 822, 8, 8, "#333", rx=1)
        s.text(x, 846, name, 9, bold=True, anchor="middle")
        s.path(f"M{x},822 V814", color)
        s.tag(x, 806, label, color)
    s.text(490, 868, "IMU LSM6DSO32: opzionale (Fase 7)", 12, bold=True, anchor="middle")
    s.text(490, 885, "SDA e SCL in parallelo al display: nessun pin in più", 10, anchor="middle")
    s.text(490, 900, "rigido sul telaio, assi allineati alla macchina", 10, anchor="middle")
    s.text(490, 915, "breakout di circa 25 × 18 mm", 10, color="#555", anchor="middle")


def gm10(s):
    """The GNSS module: its TX to D7, its RX from D6 (a swapped pair looks like a dead module)."""
    s.rect(1350, 400, 200, 140, "#fde8d8")
    s.text(1450, 446, "GM10", 15, bold=True, anchor="middle")
    s.text(1450, 464, "Flywoo GOKU (u-blox M10)", 10, anchor="middle")
    s.text(1458, 416, "5V", 10, bold=True)
    s.text(1360, 484, "TX", 10, bold=True)
    s.text(1360, 504, "RX", 10, bold=True)
    s.path("M1450,540 V552", NEG)
    s.gnd(1450, 560)
    s.path(f"M1350,480 H1290 V{pad_y('D7')} H{XR}", UART)
    s.path(f"M{XL},{pad_y('D6')} H990 V780 H1320 V500 H1350", UART)
    s.text(1284, 600, "TX → D7", 11, bold=True, color=UART, anchor="end")
    s.text(1326, 650, "RX ← D6", 11, bold=True, color=UART)


def legend(s, warnings):
    rows = [("line", POS, "+ positivo (BEC o 5 V)"), ("line", NEG, "− negativo / massa"),
            ("line", SIG, "segnale RC (PWM)"), ("line", SDA, "SDA, dati I²C"), ("line", SCL, "SCL, clock I²C"),
            ("line", UART, "seriale del GNSS"), ("gnd", None, "tutti i GND sono lo stesso filo (− ricevente = GND XIAO)"),
            ("3v3", None, "tutti i 3V3 sono il pad 3V3 della XIAO"),
            ("x", None, "filo non usato: taglialo e isolalo"), ("y", None, "cavo a Y: il maschio va nella ricevente")]
    for k, (kind, color, label) in enumerate(rows):
        y = 990 + 21 * k
        if kind == "line":
            s.path(f"M40,{y - 4} H80", color)
        elif kind == "gnd":
            s.gnd(60, y - 4)
        elif kind == "3v3":
            s.v33(60, y - 4)
        elif kind == "x":
            s.cross(60, y - 4)
        else:
            s.rect(49, y - 13, 22, 18, "#555", rx=3)
            s.text(60, y + 1, "Y", 11, bold=True, color="#fff", anchor="middle")
        s.text(92, y, label, 12)
    s.rect(900, 975, 760, 240, "#fff1f1", stroke="#c62828", rx=8)
    s.text(916, 1000, "ATTENZIONE", 14, bold=True, color="#c62828")
    for k, line in enumerate(warnings):
        s.text(916, 1026 + 24 * k, line, 13)


# --- The bench ---------------------------------------------------------------------------------

def bench():
    s = Svg()
    s.text(30, 40, "Oggi, al banco (come hardware/bench.kicad_sch)", 24, bold=True)
    s.text(30, 66, "La XIAO va con l'USB, che alimenta anche display e GM10. Dalla ricevente prendi solo "
                   "segnali e massa: il suo + NON si collega.", 15, color="#444")
    for k, line in enumerate((
            "• Per leggere un canale servono due fili: il segnale S e la massa −. Il + non serve.",
            "• Il cavo a Y lascia servo ed ESC collegati come sempre e ti dà una presa in parallelo.",
            "• Basta un − solo: dentro la ricevente tutti i − sono già collegati tra loro.",
            "• I 10 kΩ in serie vanno bene solo con riceventi a 3,3 V, come la KR-331 (misurata).",
            "• La ricevente deve essere accesa: batteria della macchina collegata all'ESC, ESC acceso.")):
        s.text(40, 112 + 24 * k, line, 14)
    receiver(s, "libera")
    y_cable(s, PORTS["CH2"], ("ESC Brainz 8", "BEC 5,9 V: alimenta la ricevente"), up=True)
    y_cable(s, PORTS["CH1"], ("Servo sterzo", ""), up=False)
    taps(s, ground_on_throttle=True)
    yt, ys = PORTS["CH2"] + 10, PORTS["CH1"] + 10
    s.path(f"M362,{yt} H560 M610,{yt} H{XL}", SIG)
    s.resistor_h(560, 610, yt, "R4 10 kΩ")
    s.path(f"M362,{ys} H560 M610,{ys} H800 V{pad_y('D3')} H{XL}", SIG)
    s.resistor_h(560, 610, ys, "R3 10 kΩ")
    s.text(940, yt - 6, "gas", 11, bold=True, color="#9a6b00")
    s.text(930, pad_y("D3") - 6, "sterzo", 11, bold=True, color="#9a6b00")
    xiao(s, {"D0", "D2", "D3", "D4", "D5", "D6", "D7", "5V", "GND", "3V3"})
    keys(s)
    display(s)
    gm10(s)
    s.path(f"M{XR},{pad_y('5V')} H1250 V380 H1450 V400", POS)
    s.text(1300, 372, "5V del GM10 dal pad 5V", 11, bold=True, color=POS)
    s.path("M1100,412 V386", "#555", 2.5)
    s.add('<polygon points="1092,392 1108,392 1100,380" fill="#555"/>')
    s.text(1100, 372, "USB-C → computer", 12, bold=True, anchor="middle")
    legend(s, ["✗  Non collegare il + della ricevente: la XIAO va con l'USB",
               "✗  Mai il 3V3 della XIAO verso il + della ricevente",
               "✓  Massa in comune: senza, i segnali non si leggono",
               "✓  Ordine dei pin S / + / −: controlla la serigrafia della ricevente",
               "✓  Display e GM10: i loro pin hanno nomi stampati, segui quelli",
               "In macchina gli ingressi RC hanno anche BAT43 e 100 kΩ (wiring-car)"])
    s.save(f"{OUT}/wiring-bench.svg")


# --- The car -------------------------------------------------------------------------------------

def to220(s, x, top, name, sub, legs, tab):
    """A TO-220 as seen from its printed face, legs down; returns the legs' x."""
    s.rect(x - 45, top, 90, 42, "#b8bec6", stroke="#666", width=1.5, rx=3)
    s.add(f'<circle cx="{x}" cy="{top + 20}" r="9" fill="#fff" stroke="#666" stroke-width="1.5"/>')
    s.rect(x - 45, top + 40, 90, 80, "#1e1e1e", stroke="#1e1e1e", width=1, rx=3)
    xs = (x - 27, x, x + 27)
    for lx, label in zip(xs, legs):
        s.rect(lx - 3, top + 120, 6, 35, "#9aa0a6", stroke="#9aa0a6", width=0.5, rx=0)
        s.text(lx, top + 112, label, 9, bold=True, color="#fff", anchor="middle")
    s.text(x, top + 70, name, 12, bold=True, color="#fff", anchor="middle")
    s.text(x, top + 86, sub, 10, color="#ccc", anchor="middle")
    s.text(x + 52, top + 24, tab, 11, color="#555")
    return xs


def car():
    s = Svg()
    s.text(30, 40, "In macchina (progetto: hardware/car.kicad_sch)", 24, bold=True)
    s.text(30, 66, "Kyosho MP9e + ricevente KR-331. Alimentazione dalla presa VCC della ricevente "
                   "attraverso l'LM2940; nessun diodo.", 15, color="#444")
    s.text(40, 118, "Alimentazione", 14, bold=True)
    for k, line in enumerate(("1.  + e − dalla presa VCC della ricevente (è libera)",
                              "2.  + → C1, C2 → IN dell'LM2940",
                              "3.  OUT → C3 → pad 5V della XIAO e 5V del GM10",
                              "4.  − → GND: massa in comune con tutto")):
        s.text(40, 140 + 20 * k, line, 13)
    s.text(40, 230, "Niente diodi: mai la USB con la board alimentata", 13, bold=True, color="#c62828")
    s.text(40, 248, "dalla macchina o da una LiPo.", 13, bold=True, color="#c62828")

    receiver(s, "alimentazione")
    yv = PORTS["VCC"]
    s.path(f"M262,{yv - 10} H280 V386", NEG)
    s.gnd(280, 376)
    s.path(f"M262,{yv + 10} H272", SIG)
    s.cross(278, yv + 10)
    rail = 290
    s.path(f"M262,{yv} H320 V{rail} H613", POS)
    s.text(330, rail - 8, "+ BEC ≈ 5,9 V", 12, bold=True, color=POS)
    s.cap(420, rail, 340, "C1 470 µF", "low-ESR", polar=True)
    s.cap(510, rail, 340, "C2 100 nF", "ceramico", polar=False)
    ins, gnd, out = to220(s, 640, 110, "LM2940", "CT-5.0", ("IN", "GND", "OUT"), "aletta = GND")
    s.path(f"M{ins},265 V{rail}", POS)
    s.path(f"M{gnd},265 V300", NEG)
    s.gnd(gnd, 310)
    s.path(f"M{out},265 V{rail} H1450 V400", POS)
    s.path(f"M1250,{rail} V{pad_y('5V')} H{XR}", POS)
    s.dot(1250, rail, POS)
    s.cap(730, rail, 340, "C3 100 µF", "low-ESR, EEU-FR1C101", polar=True)
    s.text(780, rail - 8, "+5 V", 12, bold=True, color=POS)
    s.text(1460, 380, "5V del GM10", 11, bold=True, color=POS)

    y_cable(s, PORTS["CH2"], ("ESC Brainz 8", "BEC 5,9 V: alimenta la ricevente"), up=True)
    y_cable(s, PORTS["CH1"], ("Servo sterzo", ""), up=False)
    taps(s, ground_on_throttle=False)
    # The RC inputs' protection (§2.5): 10 kΩ in series, a BAT43 to 3V3, 100 kΩ to ground. The
    # steering node sits further right, so that its clamp clears the throttle's pull-down.
    s.rect(545, 478, 300, 247, "none", stroke="#999", width=1.5, rx=8, dash="6 4")
    s.text(552, 718, "protezione ingressi RC", 11, color="#777", italic=True)
    for name, node, rs, d, rp, to in (("CH2", 660, "R4 10 kΩ", "D2 BAT43", "R6 100 kΩ", None),
                                      ("CH1", 740, "R3 10 kΩ", "D1 BAT43", "R5 100 kΩ", "D3")):
        y = PORTS[name] + 10
        end = f"H{XL}" if to is None else f"H830 V{pad_y(to)} H{XL}"
        s.path(f"M362,{y} H560 M610,{y} {end}", SIG)
        s.resistor_h(560, 610, y, rs)
        s.dot(node, y, SIG)
        s.diode_up(node, y, y - 50, d)
        s.v33(node, y - 58)
        s.resistor_v(node, y, y + 44, rp)
        s.gnd(node, y + 52)
    s.text(940, PORTS["CH2"] + 4, "gas", 11, bold=True, color="#9a6b00")
    s.text(930, pad_y("D3") - 6, "sterzo", 11, bold=True, color="#9a6b00")

    xiao(s, {"D0", "D2", "D3", "D4", "D5", "D6", "D7", "5V", "GND", "3V3"})
    s.text(1100, 398, "USB-C: mai con la macchina accesa", 11, bold=True, color="#c62828", anchor="middle")
    keys(s)
    display(s)
    imu(s)
    gm10(s)
    s.text(1000, 830, "Sul retro: BAT+ / BAT− liberi, niente batteria tampone", 12, bold=True)
    s.text(1000, 846, "la board si spegne con la macchina: prima chiudi la sessione (tieni OK)", 11,
           color="#555")
    legend(s, ["✗  Mai la USB mentre la macchina o una LiPo alimentano la board",
               "✗  Mai il + della ricevente dritto sul pad 5V: passa dall'LM2940",
               "✗  Mai il 3V3 della XIAO verso il + della ricevente",
               "✓  Massa in comune: senza, i segnali non si leggono",
               "✓  Ordine dei pin S / + / −: controlla la serigrafia della ricevente",
               "✓  C3 elettrolitico low-ESR (EEU-FR1C101): mai un ceramico da solo",
               "✓  Prima della XIAO: a macchina accesa, 5,0 V sull'OUT dell'LM2940",
               "✓  BAT43: la fascia sul corpo è il catodo, verso il 3V3"])
    s.save(f"{OUT}/wiring-car.svg")


bench()
car()
