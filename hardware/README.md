# Hardware

KiCad schematics of the device, one sheet each:

| Sheet | What | Status |
| --- | --- | --- |
| [`bench.kicad_sch`](bench.kicad_sch) ([PDF](bench.pdf)) | The bench build: the XIAO, the display, the two keys, the GM10, and the taps on the receiver's channels | **As wired** on 2026-10-19 |
| [`car.kicad_sch`](car.kicad_sch) ([PDF](car.pdf)) | In the car: the power path from the receiver rail, the backup LiPo, the 74LVC2G17 on the RC inputs, the JS5208 switch, the IMU | **Planned**, from PLAN-ESP32.md §2.5, §2.7 and §3.1 |

![Bench build](bench.png)

![In the car](car.png)

The signals are the same on both sheets, with the names of `firmware/include/pins.h`: `KEYS` on D0,
`RC_THROTTLE` on D2, `RC_STEER` on D3, `SDA` and `SCL` on D4 and D5, and the GNSS UART as
`D6_to_GPS_RX` and `GPS_TX_to_D7`. Those two are named for where they go because a swapped pair
looks like a dead module (`docs/hardware.md`). The car sheet changes the power, the RC front end and
the keys; anything else differs only because a part is not on the bench yet.

## Reading and editing

Open a `.kicad_sch` in KiCad's Schematic Editor; the sheets are checked with KiCad 10.0.1. The
symbols are embedded, so no library setup is needed. The modules — XIAO, display, GM10, receiver,
ESC, servo, JS5208, IMU breakout, buck — are drawn as boxes in a library named `rct`. Power is on
top of each box, ground below, and the signals on its sides. The XIAO's pin numbers are its pads, so
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
overwrites the sheets, edits made in KiCad included, so edit in KiCad from now on.
