// RC Telemetry firmware entry point.
//
// What lands here, in the order ROADMAP.md sets out: the power-on self-test (§3.3) runs
// synchronously in setup(), before any task exists, and only the tasks whose peripherals it left
// standing are then created (§4.1). loop() stays empty apart from watchdog feeding and
// housekeeping.
//
// So far: the POST, the ui task with the menu on the keys and the display (ROADMAP.md phases 2
// and 3), and the gnss task on the module check 9 configured (steps 4.1 and 4.3).
#include <Arduino.h>

#include "config.h"
#include "gnss_task.h"
#include "i2c_bus.h"
#include "keys.h"
#include "pins.h"
#include "post.h"
#include "settings.h"
#include "ui_fsm.h"

namespace {

// A host that opens the port after the POST still gets its log: TinyUSB drops what is printed while
// no host listens, and an upload's reset re-enumerates the port, so the boot is over before a
// monitor can reopen it. The connection is the host asserting DTR and RTS, as pyserial and the IDE
// monitors do.
volatile bool hostOpened = false;

void onHostOpened(void*, esp_event_base_t, int32_t, void*) { hostOpened = true; }

}  // namespace

void setup() {
  Serial.onEvent(ARDUINO_USB_CDC_CONNECTED_EVENT, onHostOpened);
  Serial.begin(115200);
  Serial.printf("\nRC Telemetry %s\n", FW_VERSION);

  i2cBusBegin();
  keysBegin();                      // before the POST, whose fatal latch polls them
  ui::PostResult post = postRun();  // check 0 brings the display up; a missing one boots headless
  keysSetLayout(settings().keysLayout == 1 ? KeyLayout::FiveWay : KeyLayout::TwoButtons);
  gnssTaskStart();  // when check 8 found the module; it retries check 9 if that went unanswered
  uiStart(post);    // after it: acquisition is timed from the gnss task's start
}

void loop() {
  if (hostOpened) {  // during the POST too: the log is echoed once it is written
    hostOpened = false;
    postEchoLog();
  }
  vTaskDelay(pdMS_TO_TICKS(100));  // nothing else to do here; the work is in the tasks
}
