#include "ui_keys.h"

namespace ui {
namespace {

// Wrap-safe "a is at or after b" for millisecond counters.
bool reached(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) >= 0; }

}  // namespace

Key classify(uint16_t mv, const uint16_t (&boundsMv)[5]) {
  for (uint8_t i = 0; i < 5; ++i) {
    if (mv < boundsMv[i]) return static_cast<Key>(i);
  }
  return Key::None;
}

const char* keyName(Key key) {
  static const char* const kNames[] = {"OK", "UP", "DOWN", "LEFT", "RIGHT", "none"};
  return kNames[static_cast<uint8_t>(key)];
}

const char* eventName(Event event) {
  static const char* const kNames[] = {"press", "up", "down", "back", "open", "hold"};
  return kNames[static_cast<uint8_t>(event)];
}

// --- KeyReader ----------------------------------------------------------------

bool KeyReader::sample(uint16_t mv) {
  Key key = classify(mv, config_.boundsMv);
  if (key == candidate_) {
    if (run_ < config_.confirmSamples) ++run_;
  } else {
    candidate_ = key;
    run_ = 1;
  }
  if (run_ < config_.confirmSamples || key == key_) return false;
  key_ = key;
  return true;
}

// --- Gestures -----------------------------------------------------------------

Key Gestures::normalize(Key key) const {
  // Two buttons can only produce OK and NAV. Anything else is noise, and is treated as no key.
  if (config_.layout == Layout::TwoButtons && key != Key::Ok && key != config_.navKey) {
    return Key::None;
  }
  return key;
}

void Gestures::update(Key held, uint32_t nowMs) {
  held = normalize(held);
  if (held == held_) {
    if (held_ != Key::None) hold(nowMs);
    return;
  }
  // One key straight to another — two buttons, the first released while the second is held — is a
  // release followed by a press.
  if (held_ != Key::None) release(nowMs);
  held_ = held;
  if (held_ != Key::None) press(held_, nowMs);
}

void Gestures::press(Key key, uint32_t nowMs) {
  downAtMs_ = nowMs;
  fired_ = false;
  swallowed_ = false;
  push(Event::Press);

  // OK, and NAV in the two-button layout, are decided by how long they are held, not here.
  if (key == Key::Ok) return;
  if (config_.layout == Layout::TwoButtons) return;
  switch (key) {
    case Key::Up:
    case Key::Down:
      push(key == Key::Up ? Event::Up : Event::Down);
      nextRepeatMs_ = nowMs + config_.repeatDelayMs;
      break;
    case Key::Left:
      push(Event::Back);
      break;
    case Key::Right:
      push(Event::Open);  // RIGHT is OK short, at the press
      break;
    default:
      break;
  }
}

void Gestures::hold(uint32_t nowMs) {
  if (swallowed_) return;
  uint32_t heldMs = nowMs - downAtMs_;

  if (held_ == Key::Ok) {
    if (!fired_ && heldMs >= config_.longMs) {
      push(Event::Hold);
      fired_ = true;
    }
    return;
  }
  if (config_.layout == Layout::TwoButtons) {  // held_ is NAV
    if (!fired_ && heldMs >= config_.navBackMs) {
      push(Event::Back);
      fired_ = true;
    }
    return;
  }
  if ((held_ == Key::Up || held_ == Key::Down) && reached(nowMs, nextRepeatMs_)) {
    push(held_ == Key::Up ? Event::Up : Event::Down);
    nextRepeatMs_ += config_.repeatMs;
  }
}

void Gestures::release(uint32_t nowMs) {
  if (swallowed_ || fired_) return;
  uint32_t heldMs = nowMs - downAtMs_;

  if (held_ == Key::Ok) {
    // Released between shortMaxMs and the hold: deliberately nothing, the bar just empties.
    if (heldMs < config_.shortMaxMs) push(Event::Open);
    return;
  }
  if (config_.layout == Layout::TwoButtons) push(Event::Down);  // NAV released before navBackMs
}

uint8_t Gestures::holdPercent(uint32_t nowMs) const {
  if (held_ != Key::Ok || fired_ || swallowed_) return 0;
  uint32_t heldMs = nowMs - downAtMs_;
  if (heldMs < config_.holdBarFromMs) return 0;
  if (heldMs >= config_.longMs) return 100;
  return static_cast<uint8_t>((heldMs - config_.holdBarFromMs) * 100u /
                              (config_.longMs - config_.holdBarFromMs));
}

void Gestures::push(Event event) {
  if (count_ == kQueueSize) return;  // cannot happen when drained every tick; drop rather than wrap
  queue_[(head_ + count_) % kQueueSize] = event;
  ++count_;
}

bool Gestures::next(Event& event) {
  if (count_ == 0) return false;
  event = queue_[head_];
  head_ = (head_ + 1) % kQueueSize;
  --count_;
  return true;
}

}  // namespace ui
