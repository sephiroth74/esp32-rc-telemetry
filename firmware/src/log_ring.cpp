#include "log_ring.h"

#include <Arduino.h>

#include "config.h"

namespace {

uint8_t* ring = nullptr;

}  // namespace

bool logRingBegin() {
  if (!ring && psramFound()) ring = static_cast<uint8_t*>(ps_malloc(LOG_RING_BYTES));
  return ring != nullptr;
}

uint8_t* logRing() { return ring; }

size_t logRingBytes() { return ring ? LOG_RING_BYTES : 0; }
