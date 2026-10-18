// RC channel, live, on a Seeed XIAO ESP32-S3 Sense: one channel's pulse width as a bar that grows
// left or right from the centre of the OLED, for a demo with the car on the bench.
//
// The pulses are timed by the MCPWM capture unit, as the firmware will time them (PLAN-ESP32.md
// §4.3, ROADMAP.md step 6.2): both edges are stamped in hardware at 80 MHz, so widths are good to
// the microsecond, where tools/rc-scope's ADC readings are 50 µs apart. The bar is two of the
// panel's eight pages, sent on their own (§2.4): about 8 ms of bus, so it follows the stick at up
// to 50 Hz, where a full frame would take 31 ms.
//
// Wiring. The capture needs a logic level, so there is no divider here, and this is only for a
// receiver that tools/rc-scope has measured at 3.3 V logic (docs/hardware.md, per-car table):
//   signal (white) — 10 kΩ — D3        receiver − (black) — GND        receiver + (red): nothing
//   OLED as in tools/oled-sd-test, the two buttons on D0 as in tools/keys-test
// The 10 kΩ only limits the current should something go wrong. A 5 V receiver needs the front end
// of §2.5 instead.
//
// The bar's scale is fixed: 1,500 µs in the middle, 1,000 and 2,000 µs at the ends, the usual span
// of a surface radio; beyond them it stops at the end. Keys: OK names the next channel on screen,
// CH1 → CH2 → CH3, since the wire is moved by hand; NAV clears the min and max. Serial: a `chN_us:`
// line every 40 ms, for the Arduino IDE's Serial Plotter, and nothing else.
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <driver/mcpwm_cap.h>
#include <esp_timer.h>

#include "config.h"
#include "pins.h"

constexpr uint8_t kPin = PIN_RC_STEERING;  // D3
constexpr uint8_t kOledAddr = 0x3C;

constexpr uint32_t kBarEveryMs = 20;  // at most 50 Hz; a pulse comes every 16.6 ms on the KR-331
constexpr uint32_t kTextEveryMs = 100;
constexpr uint32_t kStatsEveryMs = 250;
constexpr uint32_t kPlotEveryMs = 40;
constexpr uint32_t kKeyEveryMs = 10;
constexpr uint32_t kNoSignalMs = 100;

constexpr int32_t kNeutralUs = 1500, kHalfSpanUs = 500;
constexpr int16_t kCenterX = 64, kHalfPx = 62;    // the ends at x = 2 and x = 126
constexpr int16_t kBarTop = 24, kBarHeight = 16;  // pages 3 and 4
constexpr uint8_t kPageTiles = 128 / 8;

// The full-buffer variant: draw anywhere, then send only the pages that changed.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
bool oledOk = false;
bool captureOk = false;
uint8_t channel = 1;  // only the label: the wire picks the channel

// --- Capture ------------------------------------------------------------------

mcpwm_cap_timer_handle_t capTimer = nullptr;
mcpwm_cap_channel_handle_t capChannel = nullptr;
uint32_t ticksPerUs = 80;

// Filled by the capture interrupt and read by the loop, both under captureLock.
portMUX_TYPE captureLock = portMUX_INITIALIZER_UNLOCKED;
struct Captured {
  uint32_t pulses = 0;  // valid ones, since boot: a new one has come when it changes
  uint32_t lastWidthUs = 0, lastPeriodUs = 0;
  uint32_t minUs = UINT32_MAX, maxUs = 0;  // since NAV
  uint32_t rejected = 0;
  int64_t lastValidUs = INT64_MIN / 2;
} captured;

// The interrupt's own state.
uint32_t riseTicks = 0, periodTicks = 0;
bool seenRise = false;     // riseTicks is real
bool risen = false;        // and its falling edge has not come yet
bool periodKnown = false;  // periodTicks spans two real rising edges

// A pulse counts when its width and its frame fit the windows of config.h (§4.3); anything else —
// a floating input, a wire being plugged in — is counted and dropped.
bool IRAM_ATTR onEdge(mcpwm_cap_channel_handle_t, const mcpwm_capture_event_data_t* e, void*) {
  if (e->cap_edge == MCPWM_CAP_EDGE_POS) {
    if (seenRise) {
      periodTicks = e->cap_value - riseTicks;
      periodKnown = true;
    }
    riseTicks = e->cap_value;
    seenRise = risen = true;
    return false;
  }
  if (!risen) return false;
  risen = false;
  uint32_t width = (e->cap_value - riseTicks) / ticksPerUs;
  uint32_t period = periodKnown ? periodTicks / ticksPerUs : 0;
  bool valid = width >= RC_PULSE_MIN_US && width <= RC_PULSE_MAX_US && period >= RC_FRAME_MIN_US &&
               period <= RC_FRAME_MAX_US;
  portENTER_CRITICAL_ISR(&captureLock);
  if (valid) {
    ++captured.pulses;
    captured.lastWidthUs = width;
    captured.lastPeriodUs = period;
    if (width < captured.minUs) captured.minUs = width;
    if (width > captured.maxUs) captured.maxUs = width;
    captured.lastValidUs = esp_timer_get_time();
  } else {
    ++captured.rejected;
  }
  portEXIT_CRITICAL_ISR(&captureLock);
  return false;
}

bool captureBegin() {
  mcpwm_capture_timer_config_t timerConfig = {};
  timerConfig.group_id = 0;
  timerConfig.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT;
  if (mcpwm_new_capture_timer(&timerConfig, &capTimer) != ESP_OK) return false;
  uint32_t hz = 0;
  if (mcpwm_capture_timer_get_resolution(capTimer, &hz) != ESP_OK || hz < 1000000) return false;
  ticksPerUs = hz / 1000000;

  mcpwm_capture_channel_config_t channelConfig = {};
  channelConfig.gpio_num = kPin;
  channelConfig.prescale = 1;
  channelConfig.flags.pos_edge = true;
  channelConfig.flags.neg_edge = true;
  if (mcpwm_new_capture_channel(capTimer, &channelConfig, &capChannel) != ESP_OK) return false;

  mcpwm_capture_event_callbacks_t callbacks = {};
  callbacks.on_cap = onEdge;
  return mcpwm_capture_channel_register_event_callbacks(capChannel, &callbacks, nullptr) ==
             ESP_OK &&
         mcpwm_capture_channel_enable(capChannel) == ESP_OK &&
         mcpwm_capture_timer_enable(capTimer) == ESP_OK &&
         mcpwm_capture_timer_start(capTimer) == ESP_OK;
}

Captured snapshot() {
  portENTER_CRITICAL(&captureLock);
  Captured c = captured;
  portEXIT_CRITICAL(&captureLock);
  return c;
}

bool hasSignal(const Captured& c) {
  return captureOk && esp_timer_get_time() - c.lastValidUs < int64_t(kNoSignalMs) * 1000;
}

// --- The panel, a region at a time --------------------------------------------

// Clears the pages [firstPage, firstPage + pages) in the buffer, for a region to be redrawn.
void clearPages(uint8_t firstPage, uint8_t pages) {
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, firstPage * 8, 128, pages * 8);
  u8g2.setDrawColor(1);
}

void sendPages(uint8_t firstPage, uint8_t pages) {
  u8g2.updateDisplayArea(0, firstPage, kPageTiles, pages);
}

// The percentage of the half span, + to the right of the middle.
int32_t percentOf(uint32_t widthUs) { return (int32_t(widthUs) - kNeutralUs) * 100 / kHalfSpanUs; }

// Pages 0–1, the yellow band: channel, width, percentage.
void drawText(const Captured& c, bool signal) {
  char line[24];
  if (signal) {
    snprintf(line, sizeof line, "CH%u %4luus %+4ld%%", channel, (unsigned long)c.lastWidthUs,
             (long)percentOf(c.lastWidthUs));
  } else {
    snprintf(line, sizeof line, "CH%u   ----", channel);
  }
  clearPages(0, 2);
  u8g2.setFont(u8g2_font_6x13B_mr);
  u8g2.drawStr(0, 2, line);
  sendPages(0, 2);
}

// Pages 3–4: the bar from the middle, the middle and both ends marked, and ticks at ±50 %. The
// middle line goes when there is no signal, through which it would cut.
void drawBar(const Captured& c, bool signal) {
  clearPages(3, 2);
  if (signal) {
    int32_t offset = int32_t(c.lastWidthUs) - kNeutralUs;
    if (offset > kHalfSpanUs) offset = kHalfSpanUs;
    if (offset < -kHalfSpanUs) offset = -kHalfSpanUs;
    int16_t length = offset * kHalfPx / kHalfSpanUs;
    if (length > 0) u8g2.drawBox(kCenterX, kBarTop + 3, length, kBarHeight - 6);
    if (length < 0) u8g2.drawBox(kCenterX + length, kBarTop + 3, -length, kBarHeight - 6);
    u8g2.drawVLine(kCenterX, kBarTop, kBarHeight);
  } else {
    u8g2.setFont(u8g2_font_6x13B_mr);
    u8g2.drawStr(captureOk ? 37 : 34, kBarTop + 2, captureOk ? "NO SIGNAL" : "NO CAPTURE");
  }
  u8g2.drawVLine(kCenterX - kHalfPx - 1, kBarTop, kBarHeight);
  u8g2.drawVLine(kCenterX + kHalfPx, kBarTop, kBarHeight);
  for (int16_t x : {kCenterX - kHalfPx / 2, kCenterX + kHalfPx / 2}) {
    u8g2.drawVLine(x, kBarTop, 2);
    u8g2.drawVLine(x, kBarTop + kBarHeight - 2, 2);
  }
  sendPages(3, 2);
}

// Page 6: the range since NAV, the frame period, and the pulses dropped.
void drawStats(const Captured& c, bool signal) {
  char line[32];
  if (c.maxUs) {
    snprintf(line, sizeof line, "%4lu-%4luus %4.1fms r%lu", (unsigned long)c.minUs,
             (unsigned long)c.maxUs, signal ? c.lastPeriodUs / 1000.0f : 0.0f,
             (unsigned long)c.rejected);
  } else {
    snprintf(line, sizeof line, "no pulses yet     r%lu", (unsigned long)c.rejected);
  }
  clearPages(6, 1);
  u8g2.setFont(u8g2_font_5x8_mr);
  u8g2.drawStr(0, 48, line);
  sendPages(6, 1);
}

// Pages 2, 5 and 7, which never change: the scale under the bar and the keys.
void drawStatic() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_mr);
  u8g2.drawStr(0, 40, "1000");
  u8g2.drawStr(kCenterX - 10, 40, "1500");
  u8g2.drawStr(128 - 20, 40, "2000");
  u8g2.drawStr(0, 56, "OK: channel  NAV: clear");
  u8g2.sendBuffer();
}

// --- Keys ---------------------------------------------------------------------

enum class Key : uint8_t { None, Ok, Nav, Other };

// The windows of config.h, as tools/keys-test checked them on the two-button ladder.
Key readKey() {
  uint16_t mv = analogReadMilliVolts(PIN_KEYS);
  uint8_t window = 0;
  while (window < 5 && mv >= KEY_MV_BOUNDS[window]) ++window;
  if (window == 0) return Key::Ok;
  if (window == KEY_NAV_WINDOW) return Key::Nav;
  return window == 5 ? Key::None : Key::Other;
}

void clearRange() {
  portENTER_CRITICAL(&captureLock);
  captured.minUs = UINT32_MAX;
  captured.maxUs = 0;
  captured.rejected = 0;
  portEXIT_CRITICAL(&captureLock);
}

void pollKeys() {
  static Key candidate = Key::None, current = Key::None;
  static uint8_t run = 0;
  Key k = readKey();
  run = k == candidate ? run + 1 : 1;
  candidate = k;
  if (run < KEY_CONFIRM_SAMPLES || k == current) return;
  current = k;
  if (k == Key::Ok) {
    channel = channel % 3 + 1;
    clearRange();
  } else if (k == Key::Nav) {
    clearRange();
  }
}

// --- Main ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);  // no banner: every line is for the plotter

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(kOledAddr);  // nothing is sent to a display that did not answer
  oledOk = Wire.endTransmission() == 0;
  if (oledOk) {
    u8g2.setI2CAddress(kOledAddr * 2);  // U8g2 takes the 8-bit form
    u8g2.setBusClock(400000);
    u8g2.begin();
    u8g2.setFontPosTop();
    drawStatic();
  }

  pinMode(PIN_KEYS, INPUT);  // the ladder's own 10 kΩ holds D0 high
  captureOk = captureBegin();
}

void loop() {
  uint32_t nowMs = millis();
  static uint32_t lastKeyMs = 0, lastBarMs = 0, lastTextMs = 0, lastStatsMs = 0, lastPlotMs = 0;
  static uint32_t lastPulses = 0;
  if (nowMs - lastKeyMs >= kKeyEveryMs) {
    lastKeyMs = nowMs;
    pollKeys();
  }

  Captured c = snapshot();
  bool signal = hasSignal(c);
  if (nowMs - lastPlotMs >= kPlotEveryMs) {
    lastPlotMs = nowMs;
    if (signal) Serial.printf("ch%u_us:%lu\n", channel, (unsigned long)c.lastWidthUs);
  }
  if (!oledOk) return;

  // The bar on every new pulse, at most every kBarEveryMs; without a signal, now and then.
  bool newPulse = c.pulses != lastPulses;
  uint32_t barDue = signal && newPulse ? kBarEveryMs : kStatsEveryMs;
  if (nowMs - lastBarMs >= barDue) {
    lastBarMs = nowMs;
    lastPulses = c.pulses;
    drawBar(c, signal);
  }
  if (nowMs - lastTextMs >= kTextEveryMs) {
    lastTextMs = nowMs;
    drawText(c, signal);
  }
  if (nowMs - lastStatsMs >= kStatsEveryMs) {
    lastStatsMs = nowMs;
    drawStats(c, signal);
  }
}
