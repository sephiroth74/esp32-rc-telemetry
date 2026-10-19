// The PSRAM ring buffer between the sampling tasks and the SD writer — PLAN-ESP32.md §4.4.
//
// Allocated by POST check 1, which is fatal without it: it is what absorbs a card that stalls for
// seconds, and a device that drops samples under a stall is not one to log with. The logger of
// phase 5 takes it over. Allocated once, whatever the number of POST retries.
#pragma once

#include <stddef.h>
#include <stdint.h>

bool logRingBegin();  // true once LOG_RING_BYTES are allocated in PSRAM
uint8_t* logRing();   // nullptr until then
size_t logRingBytes();
