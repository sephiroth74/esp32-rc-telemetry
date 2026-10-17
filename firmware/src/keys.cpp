#include "keys.h"

#include <Arduino.h>

#include "key_config.h"
#include "pins.h"

namespace {

// The layout comes from KEY_LAYOUT_DEFAULT until config.json is read (ROADMAP.md step 3.6).
const ui::KeyConfig config = keyConfig();
ui::KeyReader reader(config);
ui::Gestures gestures(config);
uint16_t lastMv = 0;

}  // namespace

void keysBegin() {
  // No internal pull-up: the ladder's own 10 kΩ holds the pin, and the ADC driver disables the
  // internal one anyway. The ADC keeps its default attenuation, the one the levels in config.h were
  // measured with (docs/hardware.md).
  pinMode(PIN_KEYS, INPUT);
}

void keysTick(uint32_t nowMs) {
  lastMv = analogReadMilliVolts(PIN_KEYS);
  reader.sample(lastMv);
  gestures.update(reader.key(), nowMs);
}

bool keysNext(ui::Event& event) { return gestures.next(event); }

void keysSwallow() { gestures.swallow(); }

uint8_t keysHoldPercent(uint32_t nowMs) { return gestures.holdPercent(nowMs); }

ui::Key keysHeld() { return reader.key(); }

uint16_t keysLastMv() { return lastMv; }
