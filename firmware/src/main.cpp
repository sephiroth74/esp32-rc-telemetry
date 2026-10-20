// RC Telemetry firmware entry point.
//
// What lands here, in the order ROADMAP.md sets out: the power-on self-test (§3.3) runs
// synchronously in setup(), before any task exists, and only the tasks whose peripherals it left
// standing are then created (§4.1). loop() stays empty apart from watchdog feeding and
// housekeeping.
//
// So far: the POST, the ui task with the menu on the keys and the display (ROADMAP.md phases 2
// and 3), and the GNSS module's configuration, check 9 (step 4.1), retried here when unanswered.
#include <Arduino.h>

#include "config.h"
#include "gnss_config.h"
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

uint32_t lastGnssTryMs = 0;

}  // namespace

void setup() {
  Serial.onEvent(ARDUINO_USB_CDC_CONNECTED_EVENT, onHostOpened);
  Serial.begin(115200);
  Serial.printf("\nRC Telemetry %s\n", FW_VERSION);

  i2cBusBegin();
  keysBegin();                      // before the POST, whose fatal latch polls them
  ui::PostResult post = postRun();  // check 0 brings the display up; a missing one boots headless
  lastGnssTryMs = millis();
  keysSetLayout(settings().keysLayout == 1 ? KeyLayout::FiveWay : KeyLayout::TwoButtons);
  uiStart(post);
}

void loop() {
  if (hostOpened) {  // during the POST too: the log is echoed once it is written
    hostOpened = false;
    postEchoLog();
  }
  // Check 9 unanswered: tried again from here, on core 1, until the gnss task of step 4.3 owns the
  // UART and takes this over.
  if (gnssConfigPending() && millis() - lastGnssTryMs >= GNSS_CFG_RETRY_MS) {
    lastGnssTryMs = millis();
    ui::CheckReport report = gnssConfigure();
    Serial.printf("GNSS config retried: %s\n", report.detail);
    gnssConfigLog(Serial);
    uiPostUpdate(9, report);
  }
  vTaskDelay(pdMS_TO_TICKS(100));
}
