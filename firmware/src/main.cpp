// RC Telemetry firmware entry point.
//
// What lands here, in the order ROADMAP.md sets out: the power-on self-test (§3.3) runs
// synchronously in setup(), before any task exists, and only the tasks whose peripherals it left
// standing are then created (§4.1). loop() stays empty apart from watchdog feeding and
// housekeeping.
//
// So far: the keys, the display, and the ui task with the menu on them (ROADMAP.md 2.3–2.6).
#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "i2c_bus.h"
#include "keys.h"
#include "pins.h"
#include "ui_fsm.h"

void setup() {
  // TODO(phase 3): drive PIN_SD_D3 high before anything else — it must be high
  // before SD_MMC.begin() or the card comes up in SPI mode (§2.3).
  Serial.begin(115200);
  Serial.printf("\nRC Telemetry %s\n", FW_VERSION);

  i2cBusBegin();
  // POST check 0 in all but name (ROADMAP.md step 3.2): a missing display must not stop the rest.
  if (!displayBegin()) Serial.printf("no display at 0x%02X: running headless\n", DISPLAY_I2C_ADDR);
  keysBegin();
  uiStart();
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));  // nothing to do here yet; the work is in the tasks
}
