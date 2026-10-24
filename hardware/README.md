# Hardware

KiCad schematics of the device, one sheet each:

| Sheet | What | Status |
| --- | --- | --- |
| [`bench.kicad_sch`](bench.kicad_sch) ([PDF](bench.pdf)) | The bench build: the XIAO, the display, the two keys, the GM10, and the taps on the receiver's channels | **As wired** on 2026-10-19 |
| [`car.kicad_sch`](car.kicad_sch) ([PDF](car.pdf)) | In the car: the power path from the receiver rail through an LM2940CT-5.0, no diodes, the backup LiPo, the RC inputs through 10 k with a BAT43 clamp and 100 k down, the two keys, the optional IMU (not fitted, DNP) | **Planned**, from PLAN-ESP32.md §2.5, §2.7 and §3.1. Rev 2, 2026-10-24: the LM2940 in place of the buck, the clamps in place of the 74LVC2G17, the bench's two keys in place of the JS5208 |

![Bench build](bench.png)

![In the car](car.png)

The signals are the same on both sheets, with the names of `firmware/include/pins.h`: `KEYS` on D0,
`RC_THROTTLE` on D2, `RC_STEER` on D3, `SDA` and `SCL` on D4 and D5, and the GNSS UART as
`D6_to_GPS_RX` and `GPS_TX_to_D7`. Those two are named for where they go because a swapped pair
looks like a dead module (`docs/hardware.md`). The car sheet changes the power and the RC inputs'
protection; anything else differs only because a part is not on the bench yet.

## Wiring pictures

Drawings of where each wire goes, in Italian, for building rather than for checking nets: the
sheets above remain the reference.

| Picture | What |
| --- | --- |
| [`wiring-bench.png`](wiring-bench.png) | The bench as wired today: each channel's signal from a Y-cable through 10 k to D2 or D3, one receiver − to GND, no receiver +; the keys, the display and the GM10, all on the USB's power |
| [`wiring-car.png`](wiring-car.png) | The car, as `car.kicad_sch`: power from the receiver's free VCC port through the LM2940CT-5.0, the Y-cables on CH1 and CH2, the RC inputs' protection, the keys, the display and the GM10 |

`gen_wiring.py` writes `wiring-bench.svg` and `wiring-car.svg`, which share their parts; edit it
and run it again rather than the SVGs. Each `.png` is rendered from its `.svg`:

```sh
cd hardware && python3 gen_wiring.py .
rsvg-convert -w 2550 wiring-car.svg > wiring-car.png      # and wiring-bench
```

## Reading and editing

Open a `.kicad_sch` in KiCad's Schematic Editor; the sheets are checked with KiCad 10.0.1. The
symbols are embedded, so no library setup is needed. The modules — XIAO, display, GM10, receiver,
ESC, servo, IMU breakout — are drawn as boxes in a library named `rct`. Power is on
top of each box, ground below, and the signals on its sides. The LM2940CT-5.0 is in `rct` too: it is
KiCad's `LM7805_TO220` under its own name, as the two share their TO-220 pins, 1 IN, 2 GND, 3 OUT. The XIAO's pin numbers are its pads, so
the box does not show where the pads are; a note on each sheet gives their order on the board.

Connections are made by net labels, so a net is every pin that touches a label of its name. In
KiCad's Schematic Editor, highlighting a net shows them all.

## Checking after a change

Electrical rules, then the nets themselves, then the exports:

```sh
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
$K sch erc -o /tmp/erc.rpt hardware/bench.kicad_sch    # and car
$K sch export netlist --format kicadsexpr -o /tmp/bench.net hardware/bench.kicad_sch
$K sch export pdf -o hardware/bench.pdf hardware/bench.kicad_sch
$K sch export svg -o /tmp/ hardware/bench.kicad_sch && rsvg-convert -w 2400 -b white /tmp/bench.svg > hardware/bench.png
```

The ERC run from the command line warns that the libraries `power`, `Device`, `rct` and the others
are not configured. Those warnings are expected: the symbols are embedded. Anything else in the
report is a real problem.

The ERC is not enough on its own. A wire that ends on another wire's end joins the two nets
silently: an early version of the car sheet had BAT+ on ground that way, with a clean ERC. Read the
netlist after any change of layout, and check that every power net holds only the pins it should.

`gen_schematics.py` wrote the first version of both sheets from KiCad's libraries. Running it again
overwrites the sheets, edits made in KiCad included, so edit in KiCad from now on. The car sheet's
rev 2 is the exception: that sheet had never been edited in KiCad, so it was generated again, with
the UUIDs of what did not change kept. To do the same, run the script into a scratch directory and
copy only the sheet you mean, or it overwrites the other one too.
