// The bench simulation — a build for testing without the car: `pio run -e sim -t upload`.
//
// Commands on the serial console stand in for the receiver and the keys, and print the screen, so
// that a computer can walk the device through a procedure on its own:
//   rc <steering us> <throttle us>    a receiver sending these widths at 60 Hz; 0 for an input off
//   pos neutral|left|right|throttle|brake|off
//                                     the bench car's widths (docs/hardware.md, bench session 5)
//   key ok|hold|nav|back              OK short, OK held, NAV short, NAV held: each a whole gesture,
//                                     its Press first, so one that closes a notice or wakes the
//                                     display does only that, as on the keys
//   screen                            the screen's lines as last drawn
// The simulated pulses go through the inputs' accepted windows like real ones. Never in the car.
#pragma once

#ifdef BENCH_SIM

#include "ui_keys.h"

void benchSimPoll();                       // reads what the console sent; every tick
bool benchSimNextEvent(ui::Event& event);  // a key waiting, if any
void benchSimSwallow();                    // keysSwallow(): the rest of the current gesture goes
bool benchSimTakeScreenRequest();          // `screen` was asked for since the last call

#endif
