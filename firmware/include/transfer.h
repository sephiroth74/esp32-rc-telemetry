// Transfer over USB: the card as a USB disk — PLAN-ESP32.md §7.3, ROADMAP.md step 9.1.
//
// The device is a composite USB device, the serial port and a disk (ARDUINO_USB_MODE=0). The disk
// has no medium until Menu › Transfer enters TRANSFER: then the FAT volume is unmounted and the
// card's sectors go to the host as they are, so that the two never both own the volume. Leaving
// takes the medium away and mounts the card again. A host that ejects the disk is told apart from
// one that still has it, which is when leaving needs a confirmation (§3.7).
#pragma once

bool transferEnter();         // false, with nothing changed, when the card does not answer
bool transferLeave();         // the card mounted again: false when it would not mount
bool transferHostAttached();  // a USB host has the device configured
bool transferEjected();       // in TRANSFER: the host has ejected the disk
