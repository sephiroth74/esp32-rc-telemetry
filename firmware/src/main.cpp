// RC Telemetry firmware entry point.
//
// Skeleton only. What lands here, in the order PLAN-ESP32.md §8 sets out:
// the power-on self-test (§3.3) runs synchronously in setup(), before any task
// exists, and only the tasks whose peripherals it left standing are then created
// (§4.1). loop() stays empty apart from watchdog feeding and housekeeping.
#include <Arduino.h>

#include "config.h"
#include "pins.h"

void setup() {
  // TODO(phase 1): put PIN_SD_D3 and PIN_LED_DATA into a safe state before
  // anything else — both float at boot and both misbehave if left alone
  // (§2.3, §2.4).
}

void loop() {
}
