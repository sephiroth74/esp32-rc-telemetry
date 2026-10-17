// Keys — PLAN-ESP32.md §3.1.
//
// The keys are one resistor ladder on one ADC pin: the JS5208 five-way switch or, until it is
// fitted, two push buttons on the same ladder. Turning ladder readings into what the menu acts on
// takes two pure stages, so both run on the host as well as on the device:
//
//   KeyReader  a reading in mV, at 100 Hz   → the key held now (its window, seen twice in a row)
//   Gestures   the key held now + the time  → events: press, up, down, back, open, hold
//
// Gestures is also where the two layouts differ — in the two-button one NAV short means DOWN and NAV
// held means LEFT — and it is the only code that knows which hardware is fitted.
//
// Framework-free (§6.1): the caller supplies the readings and the milliseconds, and the constants
// arrive through KeyConfig (firmware/include/ui_config.h builds it from config.h).
#pragma once

#include <stdint.h>

namespace ui {

// Ladder order, lowest voltage first: OK shorts the pin to GND, no key leaves it at the pull-up.
enum class Key : uint8_t { Ok, Up, Down, Left, Right, None };

enum class Layout : uint8_t { TwoButtons, FiveWay };

struct KeyConfig {
  uint16_t boundsMv[5];    // between the six levels: OK|UP|DOWN|LEFT|RIGHT|none
  uint8_t confirmSamples;  // equal readings in a row before a key counts
  Layout layout;
  Key navKey;              // two-button layout: the window NAV reads in
  uint16_t shortMaxMs;     // OK released before this: open
  uint16_t longMs;         // OK held this long: hold, fired while still held
  uint16_t holdBarFromMs;  // the hold bar starts filling here
  uint16_t repeatDelayMs;  // UP/DOWN held: the first repeat...
  uint16_t repeatMs;       // ...then one every
  uint16_t navBackMs;      // two-button layout: NAV held this long is back
};

Key classify(uint16_t mv, const uint16_t (&boundsMv)[5]);
const char* keyName(Key key);

// Accepts a key once confirmSamples consecutive readings fall in its window. Anything shorter is a
// contact bouncing or the pin passing through other windows on its way between two levels.
class KeyReader {
 public:
  explicit KeyReader(const KeyConfig& config) : config_(config) {}

  // Feeds one reading; true when the accepted key changed.
  bool sample(uint16_t mv);
  Key key() const { return key_; }

 private:
  KeyConfig config_;
  Key key_ = Key::None;
  Key candidate_ = Key::None;
  uint8_t run_ = 0;
};

// What the menu acts on (§3.1). Press opens every gesture, ahead of its other events: it is what
// closes an error, brings a dimmed display back, or answers "no" to a dialog.
enum class Event : uint8_t { Press, Up, Down, Back, Open, Hold };
const char* eventName(Event event);

class Gestures {
 public:
  explicit Gestures(const KeyConfig& config) : config_(config) {}

  // Call on every sample tick with the key held now.
  void update(Key held, uint32_t nowMs);
  // Drains the pending events in order; false when none are left.
  bool next(Event& event);
  // The rest of the current gesture produces no events: the press was consumed, e.g. by closing an
  // error. A new press starts afresh.
  void swallow() { swallowed_ = true; }
  // The hold bar: 0 until OK has been held holdBarFromMs, then up to 100 when the hold fires, and 0
  // again once it has.
  uint8_t holdPercent(uint32_t nowMs) const;

 private:
  Key normalize(Key key) const;
  void press(Key key, uint32_t nowMs);
  void hold(uint32_t nowMs);
  void release(uint32_t nowMs);
  void push(Event event);

  KeyConfig config_;
  Key held_ = Key::None;
  uint32_t downAtMs_ = 0;
  uint32_t nextRepeatMs_ = 0;
  bool fired_ = false;  // the hold (OK) or the back (NAV) of this gesture has happened
  bool swallowed_ = false;

  static constexpr uint8_t kQueueSize = 8;  // drained every tick; one gesture makes at most two
  Event queue_[kQueueSize] = {};
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};

}  // namespace ui
