#!/usr/bin/env python3
"""Writes the two wiring schematics of esp32-rc-telemetry as KiCad files: the bench build and the car.

The standard symbols are copied from KiCad's own libraries; the modules (XIAO, display, GNSS,
receiver, ESC, servo, IMU) are drawn here, and the LM2940CT-5.0 is KiCad's TO-220
regulator under its own name. Pins connect through short stubs that end in a net label or a power
symbol, and series chains through direct wires.

It wrote the first version of bench.kicad_sch and car.kicad_sch. Running it again overwrites them,
edits made in KiCad included: from now on, edit the schematics in KiCad, and keep this only to
regenerate from scratch. See README.md for the checks to run after either.

Usage: gen_schematics.py <KiCad symbol directory> <output directory>
  e.g. gen_schematics.py /Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols hardware
"""
import re
import sys
import uuid

LIBDIR, OUT = sys.argv[1], sys.argv[2]
G = 2.54  # the grid everything sits on


def uid():
    return str(uuid.uuid4())


def q(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def fmt(v):
    v = round(v, 4)
    return str(int(v)) if v == int(v) else repr(v)


# --- Library symbols ---------------------------------------------------------------------------

def lib_block(lib, name):
    t = open(f"{LIBDIR}/{lib}.kicad_sym").read()
    m = re.search(r'\n\t\(symbol ' + re.escape(q(name)), t)
    s, d = m.start() + 1, 0
    for j in range(s, len(t)):
        if t[j] == "(":
            d += 1
        elif t[j] == ")":
            d -= 1
            if d == 0:
                blk = t[s:j + 1]
                break
    return blk.replace("(symbol " + q(name), "(symbol " + q(f"{lib}:{name}"), 1)


def pins_of(block, base):
    """unit -> [(number, x, y, angle)], from the sub-symbols "<base>_<unit>_<style>"."""
    units = {}
    for m in re.finditer(r'\(symbol "' + re.escape(base) + r'_(\d+)_(\d+)"', block):
        s, d = m.start(), 0
        for j in range(s, len(block)):
            if block[j] == "(":
                d += 1
            elif block[j] == ")":
                d -= 1
                if d == 0:
                    sub = block[s:j + 1]
                    break
        unit = int(m.group(1))
        for p in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)[\s\S]*?\(number "([^"]+)"', sub):
            units.setdefault(unit, []).append((p.group(4), float(p.group(1)), float(p.group(2)), int(p.group(3))))
    return units


class Sym:
    def __init__(self, lib_id, block, base):
        self.lib_id, self.block = lib_id, block
        self.units = pins_of(block, base)

    def pins(self, unit):
        return self.units.get(0, []) + self.units.get(unit, [])


def library(lib, name):
    return Sym(f"{lib}:{name}", lib_block(lib, name), name)


def renamed(lib, name, new, **props):
    """A library symbol as rct:<new>, its sub-symbols renamed to match and the given properties
    replaced: for a part that shares a stock symbol's pins but not its name."""
    block = lib_block(lib, name).replace(q(f"{lib}:{name}"), q(f"rct:{new}"), 1)
    block = block.replace(f'(symbol "{name}_', f'(symbol "{new}_')
    for key, val in props.items():
        block = re.sub(r'\(property ' + q(key) + r' "[^"]*"', lambda m: f"(property {q(key)} {q(val)}", block,
                       count=1)
    return Sym(f"rct:{new}", block, new)


def module(name, left=(), right=(), top=(), bottom=(), width=10, value="", ref="U", types=None, spread=2,
           pitch=1):
    """A box with named pins: lists of (number, name) on each side; top and bottom ones `spread` grid
    steps apart, so that the power symbols on them have room."""
    types = types or {}
    rows = max(len(left), len(right), 1)
    w = width * G
    mt = 2 * G if top else 0  # room for the names of the top and bottom pins, drawn upright
    mb = 2 * G if bottom else 0
    h = (rows * pitch + 1) * G + mt + mb
    top_y = h / 2
    body = (f'(symbol "{name}_0_1" (rectangle (start {fmt(-w / 2)} {fmt(top_y)}) (end {fmt(w / 2)} {fmt(-top_y)}) '
            '(stroke (width 0.254) (type default)) (fill (type background))))')
    pins = []

    def pin(num, nm, x, y, a):
        t = types.get(num, "passive")
        pins.append(f'(pin {t} line (at {fmt(x)} {fmt(y)} {a}) (length {G}) '
                    f'(name {q(nm)} (effects (font (size 1.27 1.27)))) (number {q(num)} (effects (font (size 1.27 1.27)))))')
    for i, (num, nm) in enumerate(left):
        pin(num, nm, -w / 2 - G, top_y - mt - G * (i * pitch + 1), 0)
    for i, (num, nm) in enumerate(right):
        pin(num, nm, w / 2 + G, top_y - mt - G * (i * pitch + 1), 180)
    n = len(top)
    for i, (num, nm) in enumerate(top):
        pin(num, nm, (i - (n - 1) / 2) * spread * G, top_y + G, 270)
    n = len(bottom)
    for i, (num, nm) in enumerate(bottom):
        pin(num, nm, (i - (n - 1) / 2) * spread * G, -top_y - G, 90)
    block = (f'(symbol "rct:{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes) '
             f'(property "Reference" {q(ref)} (at 0 {fmt(top_y + 1.27)} 0) (effects (font (size 1.27 1.27))))'
             f' (property "Value" {q(value or name)} (at 0 {fmt(-top_y - 1.27)} 0) (effects (font (size 1.27 1.27))))'
             ' (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))'
             ' (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))'
             f' {body} (symbol "{name}_1_1" {" ".join(pins)}))')
    s = Sym(f"rct:{name}", block, name)
    s.size = (w, h)
    s.grounded = bool(bottom)
    return s


# --- A sheet -----------------------------------------------------------------------------------

OUTWARD = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}  # pin angle -> away from the body


class Sheet:
    def __init__(self, name, title, comments, paper="A4", date="2026-10-19", rev="1"):
        self.name, self.title, self.comments, self.paper = name, title, comments, paper
        self.date, self.rev = date, rev
        self.root = uid()
        self.syms, self.items = {}, []
        self.counters = {}
        self.pwr = 0

    def use(self, sym):
        self.syms[sym.lib_id] = sym

    def ref(self, prefix):
        self.counters[prefix] = self.counters.get(prefix, 0) + 1
        return f"{prefix}{self.counters[prefix]}"

    def place(self, sym, x, y, value, ref=None, rot=0, unit=1, refpos=None, valpos=None, hide_ref=False,
              hide_val=False, dnp=False):
        """Places a symbol; returns {pin number: ((x, y), outward direction)}. `dnp`: drawn, but not
        to be fitted — KiCad crosses it out."""
        self.use(sym)
        ref = ref or self.ref(sym.block.split('"Reference" "')[1].split('"')[0])
        pins = {}
        for num, px, py, a in sym.pins(unit):
            dx, dy = px, -py
            ox, oy = OUTWARD[a]
            for _ in range(rot // 90):  # counterclockwise on screen, y down
                dx, dy = dy, -dx
                ox, oy = oy, -ox
            pins[num] = ((x + dx, y + dy), (ox, oy))
        rp = refpos or (x, y - 5)
        vp = valpos or (x, y + 5)
        props = [("Reference", ref, rp, hide_ref), ("Value", value, vp, hide_val), ("Footprint", "", (x, y), True),
                 ("Datasheet", "~", (x, y), True)]
        text = [f'(symbol (lib_id {q(sym.lib_id)}) (at {fmt(x)} {fmt(y)} {rot}) (unit {unit}) (exclude_from_sim no) '
                f'(in_bom yes) (on_board yes) (dnp {"yes" if dnp else "no"}) (uuid {q(uid())})']
        fa = 90 if rot in (90, 270) else 0
        for key, val, (px, py), hide in props:
            text.append(f'(property {q(key)} {q(val)} (at {fmt(px)} {fmt(py)} {fa}) (effects (font (size 1.27 1.27))'
                        + (" (hide yes)" if hide else "") + "))")
        for num in sorted({p[0] for u in sym.units.values() for p in u}):
            text.append(f'(pin {q(num)} (uuid {q(uid())}))')
        text.append(f'(instances (project {q(self.name)} (path "/{self.root}" (reference {q(ref)}) (unit {unit}))))')
        self.items.append(" ".join(text) + ")")
        return pins

    def wire(self, *pts):
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            self.items.append(f'(wire (pts (xy {fmt(x1)} {fmt(y1)}) (xy {fmt(x2)} {fmt(y2)})) '
                              f'(stroke (width 0) (type default)) (uuid {q(uid())}))')

    def stub(self, pin, length=G):
        (x, y), (ox, oy) = pin
        end = (x + ox * length, y + oy * length)
        self.wire((x, y), end)
        return end, (ox, oy)

    def label(self, pin, name, length=G):
        (x, y), (ox, oy) = self.stub(pin, length) if length else pin
        angle = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[(ox, oy)]
        just = "left bottom" if angle in (0, 90) else "right bottom"
        self.items.append(f'(label {q(name)} (at {fmt(x)} {fmt(y)} {angle}) (effects (font (size 1.27 1.27)) '
                          f'(justify {just})) (uuid {q(uid())}))')

    def power(self, sym, at):
        """A power symbol whose pin sits at `at`."""
        self.pwr += 1
        flag = sym.lib_id == "power:PWR_FLAG"
        ref = f"#FLG0{self.pwr}" if flag else f"#PWR0{self.pwr:02d}"
        down = sym.lib_id == "power:GND"
        x, y = at
        self.place(sym, x, y, sym.lib_id.split(":")[1], ref=ref, hide_ref=True,
                   valpos=(x, y + 3.81) if down else (x, y - 3.81))

    def to_power(self, pin, sym, length=G):
        end, _ = self.stub(pin, length)
        self.power(sym, end)

    def junction(self, at):
        x, y = at
        self.items.append(f'(junction (at {fmt(x)} {fmt(y)}) (diameter 0) (color 0 0 0 0) (uuid {q(uid())}))')

    def flag_label(self, x, y, name):
        """A PWR_FLAG on a labelled net, for a power input fed through a passive part."""
        self.power(FLAG, (x, y))
        self.wire((x, y), (x, y + G))
        self.label(((x, y + G), (0, 1)), name, length=0)

    def nc(self, pin):
        (x, y), _ = pin
        self.items.append(f'(no_connect (at {fmt(x)} {fmt(y)}) (uuid {q(uid())}))')

    def text(self, x, y, s, size=1.27, bold=False):
        b = " (bold yes)" if bold else ""
        self.items.append(f'(text {q(s)} (exclude_from_sim no) (at {fmt(x)} {fmt(y)} 0) '
                          f'(effects (font (size {size} {size}){b}) (justify left top)) (uuid {q(uid())}))')

    def write(self, path):
        tb = (f'(title_block (title {q(self.title)}) (date {q(self.date)}) (rev {q(self.rev)}) '
              '(company "esp32-rc-telemetry")'
              + "".join(f" (comment {i + 1} {q(c)})" for i, c in enumerate(reversed(self.comments))) + ")")
        libs = " ".join(s.block for s in self.syms.values())
        body = "\n\t".join(self.items)
        open(path, "w").write(
            f'(kicad_sch (version 20250114) (generator "eeschema") (generator_version "9.0") (uuid {q(self.root)}) '
            f'(paper {q(self.paper)}) {tb}\n\t(lib_symbols {libs})\n\t{body}\n\t(sheet_instances (path "/" (page "1"))) '
            '(embedded_fonts no))\n')


# --- The symbols ---------------------------------------------------------------------------------

R = library("Device", "R")
C = library("Device", "C")
CP = library("Device", "C_Polarized")
DS = library("Device", "D_Schottky")
BAT = library("Device", "Battery_Cell")
SW = library("Switch", "SW_Push")
GND = library("power", "GND")
V3 = library("power", "+3V3")
V5 = library("power", "+5V")
FLAG = library("power", "PWR_FLAG")

# Power on top, ground and the battery pads below, the GPIOs on the sides; the pin numbers are the
# pads', 1-7 down the left and 8-14 up the right with USB-C at the top.
XIAO = module(
    "XIAO_ESP32S3_Sense",
    left=[("1", "D0/GPIO1"), ("2", "D1/GPIO2"), ("3", "D2/GPIO3"), ("4", "D3/GPIO4"), ("5", "D4/GPIO5/SDA"),
          ("6", "D5/GPIO6/SCL"), ("7", "D6/GPIO43/TX")],
    right=[("8", "D7/GPIO44/RX"), ("9", "D8/GPIO7/SCK"), ("10", "D9/GPIO8/MISO"), ("11", "D10/GPIO9/MOSI")],
    top=[("14", "5V"), ("12", "3V3")], bottom=[("13", "GND"), ("16", "BAT-"), ("15", "BAT+")],
    width=16, spread=4, value="Seeed XIAO ESP32-S3 Sense",
    types={"14": "power_in", "13": "power_in", "12": "power_out"})
OLED = module("SSD1306_OLED", right=[("3", "SCL"), ("4", "SDA")], top=[("2", "VCC")], bottom=[("1", "GND")],
              width=8, value="OLED 0.96in SSD1306 0x3C", ref="DS")
GPS = module("GM10", left=[("3", "TX"), ("4", "RX")], top=[("1", "5V")], bottom=[("2", "GND")], width=8,
             value="Flywoo GOKU GM10 Nano (u-blox M10)")
RX = module("Receiver_3ch", left=[("1", "CH1/S"), ("2", "CH2/S"), ("3", "CH3/S")], top=[("4", "+")],
            bottom=[("5", "-")], width=10, value="Kyosho KR-331 receiver")
SERVO = module("Servo", right=[("1", "S")], top=[("2", "+")], bottom=[("3", "-")], width=6, value="Steering servo",
               ref="M")
ESC = module("ESC", right=[("1", "S")], top=[("2", "BEC+")], bottom=[("3", "-")], width=6,
             value="ESC with BEC")
IMU = module("LSM6DSO32_breakout", right=[("3", "SCL"), ("4", "SDA")], top=[("1", "VIN")], bottom=[("2", "GND")],
             width=8, value="LSM6DSO32 IMU breakout")
# The LM2940CT-5.0 has the 7805's TO-220 pins, 1 IN, 2 GND, 3 OUT, and the tab on GND.
LDO = renamed("Regulator_Linear", "LM7805_TO220", "LM2940CT-5.0", Value="LM2940CT-5.0",
              Datasheet="https://www.ti.com/lit/ds/symlink/lm2940-n.pdf",
              Description="1 A low-dropout regulator, fixed 5 V output, TO-220")


def props(sym, x, y):
    """A module's reference above its top-left corner, its value under the box."""
    w, h = sym.size
    below = 10.16 if sym.grounded else 5.08  # under the ground symbol, when there is one
    return dict(refpos=(x - w / 2 + 2.54, y - h / 2 - 1.27), valpos=(x, y + h / 2 + below))


# --- Shared blocks --------------------------------------------------------------------------------

def xiao(s, x, y, five_volt):
    w, h = XIAO.size
    p = s.place(XIAO, x, y, "Seeed XIAO ESP32-S3 Sense", ref="U1", refpos=(x - w / 2 + 2.54, y - h / 2 - 1.27),
                valpos=(x, y + h / 2 + 12.7))
    s.label(p["1"], "KEYS")
    s.nc(p["2"])
    s.label(p["3"], "RC_THROTTLE")
    s.label(p["4"], "RC_STEER")
    s.label(p["5"], "SDA")
    s.label(p["6"], "SCL")
    s.label(p["7"], "D6_to_GPS_RX")
    s.label(p["8"], "GPS_TX_to_D7")
    for n in ("9", "10", "11"):
        s.nc(p[n])
    five_volt(p["14"])
    s.to_power(p["12"], V3)
    s.to_power(p["13"], GND)
    return p


def oled(s, x, y):
    p = s.place(OLED, x, y, "OLED 0.96in SSD1306 0x3C", ref="DS1", **props(OLED, x, y))
    s.to_power(p["2"], V3)
    s.to_power(p["1"], GND)
    s.label(p["3"], "SCL")
    s.label(p["4"], "SDA")


def gnss(s, x, y):
    p = s.place(GPS, x, y, "Flywoo GOKU GM10 Nano (u-blox M10)", ref="U2", **props(GPS, x, y))
    s.to_power(p["1"], V5)
    s.to_power(p["2"], GND)
    s.label(p["3"], "GPS_TX_to_D7")
    s.label(p["4"], "D6_to_GPS_RX")


def car_side(s, x, y):
    """The receiver as the car has it: the ESC powering it through CH2, the servo on CH1."""
    r = s.place(RX, x, y, "Kyosho KR-331 receiver", ref="U3", **props(RX, x, y))
    s.label(r["1"], "CH1_STEER")
    s.label(r["2"], "CH2_THROTTLE")
    s.nc(r["3"])
    s.label(r["4"], "RX_RAIL")
    s.to_power(r["5"], GND)
    for k, (sym, ref, val, sig) in enumerate(((ESC, "U4", "ESC with BEC (Brainz 8)", "CH2_THROTTLE"),
                                              (SERVO, "M1", "Steering servo", "CH1_STEER"))):
        sx, sy = x - 38.1, y - 20.32 + k * 40.64
        e = s.place(sym, sx, sy, val, ref=ref, **props(sym, sx, sy))
        s.label(e["1"], sig)
        s.label(e["2"], "RX_RAIL")
        s.to_power(e["3"], GND)


def keys_two_buttons(s, x0, y0):
    """The two-button ladder: a 10 k pull-up, OK straight to ground, NAV through 10 k (§3.1)."""
    r1 = s.place(R, x0, y0, "10k", ref="R1", refpos=(x0 + 2.54, y0 - 1.27), valpos=(x0 + 2.54, y0 + 1.27))
    s.to_power(r1["1"], V3)
    ybus = y0 + 7.62
    s.wire(r1["2"][0], (x0, ybus), (x0 + 25.4, ybus))
    s.wire((x0, ybus), (x0 - 5.08, ybus))
    s.label(((x0 - 5.08, ybus), (-1, 0)), "KEYS", length=0)
    s.junction((x0, ybus))
    for k, (name, ref) in enumerate((("OK", "SW1"), ("NAV", "SW2"))):
        sx = x0 + 12.7 + k * 12.7
        sw = s.place(SW, sx, ybus + 7.62, name, ref=ref, rot=90, refpos=(sx + 3.81, ybus + 6.35),
                     valpos=(sx + 3.81, ybus + 8.89))
        top = min(sw["1"][0], sw["2"][0], key=lambda pt: pt[1])
        bottom = max(sw["1"][0], sw["2"][0], key=lambda pt: pt[1])
        s.wire((sx, ybus), top)
        if k == 0:
            s.junction((sx, ybus))
            s.wire(bottom, (sx, bottom[1] + G))
            s.power(GND, (sx, bottom[1] + G))
        else:
            r2 = s.place(R, sx, bottom[1] + 6.35, "10k", ref="R2", refpos=(sx + 2.54, bottom[1] + 5.08),
                         valpos=(sx + 2.54, bottom[1] + 7.62))
            s.wire(bottom, r2["1"][0])
            s.to_power(r2["2"], GND)


def flag(s, x, y, sym):
    """A PWR_FLAG on a power net: above a ground, below a supply, so that neither covers the other."""
    if sym is GND:
        s.power(FLAG, (x, y))
        s.wire((x, y), (x, y + G))
        s.power(sym, (x, y + G))
    else:
        s.power(sym, (x, y))
        s.wire((x, y), (x, y + 5.08))
        s.pwr += 1
        s.place(FLAG, x, y + 5.08, "PWR_FLAG", ref=f"#FLG0{s.pwr}", rot=180, hide_ref=True, valpos=(x, y + 10.16))


PADS = ("XIAO pads, USB-C up, from the top: left D0 D1 D2 D3 D4 D5 D6, right 5V GND 3V3 D10 D9 D8 D7;\n"
        "BAT+ and BAT- on the back. The microSD of the Sense board takes D8 CLK, D9 DAT0, D10 CMD and\n"
        "GPIO21 for DAT3, inside: leave D8-D10 free. D1 is spare, for the optional aux channel.")


# --- Bench ------------------------------------------------------------------------------------------

def bench():
    s = Sheet("bench", "Bench build: XIAO, display, two keys, GNSS, receiver taps",
              ["As wired on 2026-10-19. Powered from USB-C; the receiver's + rail is not connected.",
               "RC inputs on the bench: 10 k in series, for receivers measured at 3.3 V logic only.",
               "Pin map: PLAN-ESP32.md 2.1. Measurements: docs/hardware.md. The car: car.kicad_sch."])
    x = xiao(s, 148.59, 93.98, lambda pin: s.to_power(pin, V5))
    s.nc(x["15"])  # no backup LiPo on the bench
    s.nc(x["16"])
    oled(s, 50.8, 50.8)
    keys_two_buttons(s, 38.1, 104.14)
    gnss(s, 248.92, 50.8)
    car_side(s, 256.54, 127)
    for k, (src, dst, ref) in enumerate((("CH1_STEER", "RC_STEER", "R3"), ("CH2_THROTTLE", "RC_THROTTLE", "R4"))):
        x, y = 149.86, 134.62 + k * 12.7
        r = s.place(R, x, y, "10k", ref=ref, rot=90, refpos=(x, y - 2.54), valpos=(x, y + 2.54))
        s.label(r["1"], dst, length=7.62)
        s.label(r["2"], src, length=7.62)
    flag(s, 20.32, 22.86, GND)
    flag(s, 38.1, 20.32, V5)
    s.text(127, 22.86, "GNSS: the module's TX to D7, the XIAO's RX; its RX to D6, the XIAO's TX. Swapped, the\n"
                       "link shows no byte at any baud rate. 5 V from the 5V pad (3V3: ROADMAP.md step 1.3).")
    s.text(20.32, 157.48, PADS)
    s.text(20.32, 172.72, "Keys: the two-button interim ladder (PLAN-ESP32.md 3.1): OK 0 V, NAV 1.62 V, none 3.12 V.\n"
                          "Receiver taps: 10 k in series, enough for the KR-331's 3.3 V logic (tools/rc-scope).\n"
                          "Unknown receiver: tools/rc-scope first, with 10 k from D3 to GND as a divider. CH1 by\n"
                          "unplugging the servo or with a Y-cable; CH2 needs a Y-cable, the ESC powering the\n"
                          "receiver through it. The receiver's + rail stays off the XIAO, which runs from USB.")
    s.write(f"{OUT}/bench.kicad_sch")


# --- Car --------------------------------------------------------------------------------------------

def car():
    s = Sheet("car", "In the car: power, backup LiPo, protected RC inputs, two keys, IMU",
              ["The design of PLAN-ESP32.md 2.5, 2.7 and 3.1: not built yet.",
               "To confirm on the bench: the LM2940 on a 5.9 V BEC, the RC inputs' clamps, the IMU.",
               "Signals and keys as on bench.kicad_sch; the power and the RC inputs' protection change."],
              paper="A3", date="2026-10-24", rev="2")
    p = xiao(s, 200.66, 101.6, lambda pin: s.to_power(pin, V5))
    # The backup LiPo across the BAT pads (§2.7).
    # BAT+ is the right-most bottom pad, so its wire and BAT-'s never cross nor touch the ground.
    (bx, by), _ = p["15"]
    (nx, ny), _ = p["16"]
    tx = bx + 12.7
    bt = s.place(BAT, tx, by + 10.16, "1S LiPo 150-300 mAh", ref="BT1", refpos=(tx + 3.81, by + 8.89),
                 valpos=(tx + 3.81, by + 11.43))
    s.wire((bx, by), (bx, bt["1"][0][1]), bt["1"][0])
    s.wire((nx, ny), (nx, bt["2"][0][1] + 2.54), (tx, bt["2"][0][1] + 2.54), bt["2"][0])

    oled(s, 63.5, 50.8)
    # Optional (§2.6): not fitted until it is bought, so drawn crossed out. It needs no GPIO.
    i = s.place(IMU, 63.5, 93.98, "LSM6DSO32 IMU breakout, optional", ref="U5", dnp=True,
                **props(IMU, 63.5, 93.98))
    s.to_power(i["1"], V3)
    s.to_power(i["2"], GND)
    s.label(i["3"], "SCL")
    s.label(i["4"], "SDA")
    gnss(s, 312.42, 50.8)

    # The keys as on the bench, two buttons on the D0 ladder (§3.1); the JS5208 can come later.
    keys_two_buttons(s, 50.8, 127)

    # The RC inputs (§2.5), per channel: 10 k in series from the receiver, a BAT43 Schottky from the
    # pin to 3V3 that clamps a 5 V receiver's pulse, and 100 k from the pin to ground that holds it
    # low with the receiver off. The second channel's pin sits further right, so that its clamp
    # clears the first one's pull-down.
    car_side(s, 368.3, 137.16)
    ux = 266.7
    for k, (src, dst, rs, d, rp) in enumerate((("CH1_STEER", "RC_STEER", "R3", "D1", "R5"),
                                                ("CH2_THROTTLE", "RC_THROTTLE", "R4", "D2", "R6"))):
        y = 129.54 + k * 22.86
        r = s.place(R, ux, y, "10k", ref=rs, rot=90, refpos=(ux, y - 2.54), valpos=(ux, y + 2.54))
        s.label(r["1"], src, length=7.62)
        nx = r["2"][0][0] + 5.08 + k * 10.16
        s.wire(r["2"][0], (nx + 7.62, y))
        s.label(((nx + 7.62, y), (1, 0)), dst, length=0)
        s.junction((nx, y))
        cl = s.place(DS, nx, y - 6.35, "BAT43", ref=d, rot=270, refpos=(nx + 3.81, y - 7.62),
                     valpos=(nx + 3.81, y - 5.08))
        s.wire((nx, y), cl["2"][0])
        s.to_power(cl["1"], V3)
        pd = s.place(R, nx, y + 6.35, "100k", ref=rp, refpos=(nx + 3.81, y + 5.08), valpos=(nx + 3.81, y + 7.62))
        s.wire((nx, y), pd["1"][0])
        s.to_power(pd["2"], GND)

    # Power: the receiver rail through the bulk caps into the LM2940, whose 5 V goes straight to the
    # 5V pad and the GM10 (§2.7). No diodes: a servo plug cannot swap + and -, and the LM2940
    # survives a reversed input on its own.
    bus = (257.81, 215.9)
    cx = bus[0] + 7.62
    s.label(((bus[0] - 2.54, bus[1]), (-1, 0)), "RX_RAIL", length=0)
    s.wire((bus[0] - 2.54, bus[1]), (cx + 20.32, bus[1]))
    # The values sit clear of the bodies, where a crossing lead would make 470u read 4700.
    for k, (sym, val, ref) in enumerate(((CP, "470u", "C1"), (C, "100n", "C2"))):
        x = cx + k * 12.7
        c = s.place(sym, x, bus[1] + 6.35, val, ref=ref, refpos=(x + 5.08, bus[1] + 5.08), valpos=(x + 5.08, bus[1] + 7.62))
        s.wire((x, bus[1]), c["1"][0])
        s.junction((x, bus[1]))
        s.to_power(c["2"], GND)
    # The LM2940's IN is a power input, and only passive parts feed it: a flag says the rail does.
    s.power(FLAG, (cx + 20.32, bus[1] - 5.08))
    s.wire((cx + 20.32, bus[1] - 5.08), (cx + 20.32, bus[1]))
    s.junction((cx + 20.32, bus[1]))
    lx = cx + 35.56
    u = s.place(LDO, lx, bus[1], "LM2940CT-5.0", ref="U6", refpos=(lx, bus[1] - 6.35), valpos=(lx, bus[1] - 3.81))
    s.wire((cx + 20.32, bus[1]), u["1"][0])
    s.to_power(u["2"], GND)
    (ox, oy), _ = u["3"]
    # C3 keeps the LM2940 stable: 22 u or more, with an ESR between 0.1 and 1 ohm (its datasheet, 8.2.2.1).
    # A Panasonic EEU-FR1C101 is 0.3 ohm at 100 kHz; the small dipped tantalums run 1.3 to 2 ohm.
    c3x = ox + 5.08
    c3 = s.place(CP, c3x, oy + 6.35, "100u EEU-FR1C101", ref="C3", refpos=(c3x + 5.08, oy + 5.08),
                 valpos=(c3x + 8.89, oy + 7.62))
    s.wire((c3x, oy), c3["1"][0])
    s.junction((c3x, oy))
    s.to_power(c3["2"], GND)
    s.wire((ox, oy), (ox + 12.7, oy), (ox + 12.7, oy - 5.08))
    s.power(V5, (ox + 12.7, oy - 5.08))

    flag(s, 25.4, 25.4, GND)

    s.text(20.32, 193.04, PADS)
    s.text(20.32, 208.28, "Keys: the bench's two buttons (PLAN-ESP32.md 3.1): a 10 k pull-up on D0, OK straight to\n"
                          "ground, NAV through 10 k; 0 V, 1.62 V, and 3.12 V at rest.\n"
                          "The IMU is optional (DNP): on the I2C bus with the display, at 0x6A, it needs no GPIO.\n"
                          "The GM10 runs from the LM2940's 5 V.")
    s.text(236.22, 190.5, "RC inputs, per channel: 10 k in series; a BAT43 to 3V3 that holds a 5 V receiver's pulse\n"
                           "under the pin's 3.6 V; 100 k to ground that holds the pin low with the receiver off, as the\n"
                           "RC inputs wake the chip. A 3.3 V receiver's high reads about 3.0 V, over the 2.48 V needed.")
    s.text(20.32, 226.06, "Power: never the receiver rail straight into the 5V pad. C1, low ESR, and C2 soak up the\n"
                         "servo's spikes, and the LM2940 holds 5 V for the 5V pad and the GM10. It is guaranteed from\n"
                         "6.25 V in; on a 5.9 V BEC its dropout, 0.2 V at most at 100 mA, leaves room, to confirm on\n"
                         "the bench. C3 keeps it stable: 22 u or more, ESR 0.1 to 1 ohm: a low-ESR electrolytic, the\n"
                         "EEU-FR1C101, as small tantalums run over 1 ohm. No diodes (decided 2026-10-24), so the\n"
                         "USB is never plugged in while the car or a LiPo powers the board: the two 5 V would meet\n"
                         "on the 5V pad.\n"
                         "The LiPo on the BAT pads carries the shutdown when the car is switched off.")
    s.write(f"{OUT}/car.kicad_sch")


bench()
car()
