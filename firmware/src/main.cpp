// RC Telemetry firmware entry point.
//
// What lands here, in the order ROADMAP.md sets out: the power-on self-test (§3.3) runs
// synchronously in setup(), before any task exists, and only the tasks whose peripherals it left
// standing are then created (§4.1). loop() stays empty apart from watchdog feeding and
// housekeeping.
//
// So far: the POST with check 0, and the ui task with the menu on the keys and the display
// (ROADMAP.md phase 2, step 3.1).
#include <Arduino.h>

#include "config.h"
#include "i2c_bus.h"
#include "keys.h"
#include "pins.h"
#include "post.h"
#include "ui_fsm.h"

void setup() {
  Serial.begin(115200);
  Serial.printf("\nRC Telemetry %s\n", FW_VERSION);

  i2cBusBegin();
  keysBegin();                      // before the POST, whose fatal latch polls them
  ui::PostResult post = postRun();  // check 0 brings the display up; a missing one boots headless
  uiStart(post);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));  // nothing to do here yet; the work is in the tasks
}
