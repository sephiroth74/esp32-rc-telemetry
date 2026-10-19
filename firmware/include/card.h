// The microSD card — PLAN-ESP32.md §2.3, POST checks 3–6 (§3.3).
//
// SDMMC in 1-bit mode at 40 MHz on the pins of pins.h, mounted at /sdcard, with DAT3 driven high
// first so that the card picks SD mode (§2.2). FAT32 on MBR only, the core's FatFs having neither
// exFAT nor GPT, and never formatted by the device. When the mount fails, the card is asked
// directly why: it did not answer, or its first sector shows exFAT, GPT or no FAT32 at all.
//
// The probe of checks 5 and 6 writes, reads back, compares and deletes one file, timed. The logger
// (phase 5) and Transfer (phase 9) take the card over from here.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class CardMount : uint8_t { Ok, NoCard, Exfat, Gpt, NoFat32 };

CardMount cardMount();  // POST check 3; unmounts first if it was mounted
bool cardMounted();
uint32_t cardTotalMb();
uint32_t cardFreeMb();

struct CardProbe {
  const char* failure = nullptr;  // nullptr when the data came back intact
  uint32_t writeKbps = 0;         // including the close, which is part of the cost
  uint32_t readKbps = 0;
};
CardProbe cardProbe(size_t bytes);  // POST checks 5 and 6, in 32 KB chunks (§4.4)
const CardProbe& cardLastProbe();   // for the screens and, later, meta.json
