// RC channel graph on a Seeed XIAO ESP32-S3 Sense: one channel's pulse width over time, on the
// OLED.
//
// The width in large digits in the yellow band, and the last five seconds as a strip chart that
// scrolls underneath. The pulses are timed by the MCPWM capture unit, as the firmware will time
// them (PLAN-ESP32.md §4.3, ROADMAP.md step 6.2): both edges are stamped in hardware at 80 MHz, so
// widths are good to the microsecond, where tools/rc-scope's ADC readings are 50 µs apart.
//
// Wiring. The capture needs a logic level, so there is no divider here, and this is only for a
// receiver that tools/rc-scope has measured at 3.3 V logic (docs/hardware.md, per-car table):
//   signal (white) — 10 kΩ — D3        receiver − (black) — GND        receiver + (red): nothing
//   OLED as in tools/oled-sd-test, the two buttons on D0 as in tools/keys-test
// The 10 kΩ only limits the current should something go wrong. A 5 V receiver needs the front end
// of §2.5 instead.
//
// Keys: OK names the next channel on screen, CH1 → CH2 → CH3, since the wire is moved by hand; NAV
// clears the min and max. Serial: a `chN_us:` line per graph column, for the Arduino IDE's Serial
// Plotter, and nothing else.
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <driver/mcpwm_cap.h>
#include <esp_timer.h>

#include "config.h"
#include "pins.h"

constexpr uint8_t kPin = PIN_RC_STEERING;  // D3
constexpr uint8_t kOledAddr = 0x3C;

constexpr uint32_t kColumnMs = 40;  // 128 columns: 5.1 s of history
constexpr uint32_t kDrawEveryMs = 80;
constexpr uint32_t kKeyEveryMs = 10;
constexpr uint32_t kNoSignalMs = 100;

// A fixed scale, so that channels and runs compare at a glance: 25 µs per pixel.
constexpr int32_t kGraphMinUs = 900, kGraphMaxUs = 2100;
constexpr int16_t kGraphTop = 16, kGraphBottom = 63;  // the blue part of the panel
constexpr uint8_t kColumns = 128;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);  // 4-pin modules have no reset line
bool oledOk = false;
bool captureOk = false;

// --- Capture ------------------------------------------------------------------

mcpwm_cap_timer_handle_t capTimer = nullptr;
mcpwm_cap_channel_handle_t capChannel = nullptr;
uint32_t ticksPerUs = 80;

// Filled by the capture interrupt, read and emptied by the loop, both under captureLock.
portMUX_TYPE captureLock = portMUX_INITIALIZER_UNLOCKED;
struct Captured {
  uint32_t widthSum = 0, widthCount = 0;  // the valid pulses since the last graph column
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
    captured.widthSum += width;
    ++captured.widthCount;
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

// --- The graph ----------------------------------------------------------------

uint16_t columns[kColumns];  // average width per column, 0 for a column without a valid pulse
uint8_t oldest = 0;          // the ring's next slot to write, which holds the oldest column
uint8_t channel = 1;         // only the label: the wire picks the channel

void addColumn() {
  portENTER_CRITICAL(&captureLock);
  uint32_t sum = captured.widthSum, count = captured.widthCount;
  captured.widthSum = captured.widthCount = 0;
  portEXIT_CRITICAL(&captureLock);

  uint16_t width = count ? sum / count : 0;
  columns[oldest] = width;
  oldest = (oldest + 1) % kColumns;
  if (width) Serial.printf("ch%u_us:%u\n", channel, width);
}

void clearRange() {
  portENTER_CRITICAL(&captureLock);
  captured.minUs = UINT32_MAX;
  captured.maxUs = 0;
  captured.rejected = 0;
  portEXIT_CRITICAL(&captureLock);
}

int16_t yOf(int32_t us) {
  if (us < kGraphMinUs) us = kGraphMinUs;
  if (us > kGraphMaxUs) us = kGraphMaxUs;
  return kGraphBottom -
         (us - kGraphMinUs) * (kGraphBottom - kGraphTop) / (kGraphMaxUs - kGraphMinUs);
}

void draw() {
  portENTER_CRITICAL(&captureLock);
  Captured c = captured;
  portEXIT_CRITICAL(&captureLock);
  bool signal = esp_timer_get_time() - c.lastValidUs < int64_t(kNoSignalMs) * 1000;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);

  // The yellow band: the width now, the channel and its frame period, the range since NAV.
  oled.setTextSize(2);
  oled.setCursor(0, 1);
  if (signal) {
    oled.printf("%4lu", (unsigned long)c.lastWidthUs);
  } else {
    oled.print("----");
  }
  oled.setTextSize(1);
  oled.setCursor(50, 8);
  oled.print("us");
  oled.setCursor(68, 0);
  if (signal) {
    oled.printf("CH%u %4.1fms", channel, c.lastPeriodUs / 1000.0f);
  } else {
    oled.printf("CH%u", channel);
  }
  oled.setCursor(68, 8);
  if (c.maxUs) oled.printf("%4lu-%4lu", (unsigned long)c.minUs, (unsigned long)c.maxUs);

  // The scale: 1,500 µs dotted, ticks at 1,000 and 2,000 µs on both edges.
  for (int16_t x = 0; x < 128; x += 4) oled.drawPixel(x, yOf(1500), SSD1306_WHITE);
  for (int32_t us : {1000, 2000}) {
    oled.drawFastHLine(0, yOf(us), 3, SSD1306_WHITE);
    oled.drawFastHLine(125, yOf(us), 3, SSD1306_WHITE);
  }

  // The trace, oldest on the left; a column without a pulse breaks it.
  for (uint8_t x = 1; x < kColumns; ++x) {
    uint16_t a = columns[(oldest + x - 1) % kColumns], b = columns[(oldest + x) % kColumns];
    if (a && b)
      oled.drawLine(x - 1, yOf(a), x, yOf(b), SSD1306_WHITE);
    else if (b)
      oled.drawPixel(x, yOf(b), SSD1306_WHITE);
  }

  if (!signal || !captureOk) {
    oled.fillRect(16, 28, 96, 22, SSD1306_BLACK);
    oled.drawRect(16, 28, 96, 22, SSD1306_WHITE);
    oled.setCursor(37, 31);
    oled.print(captureOk ? "NO SIGNAL" : "NO CAPTURE");
    oled.setCursor(22, 40);
    oled.printf("rejected %lu", (unsigned long)c.rejected);
  }
  oled.display();
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
    memset(columns, 0, sizeof columns);
  } else if (k == Key::Nav) {
    clearRange();
  }
}

// --- Main ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);  // no banner: every line is for the plotter

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(kOledAddr);  // begin() succeeds whether or not anything answers
  oledOk = Wire.endTransmission() == 0 &&
           oled.begin(SSD1306_SWITCHCAPVCC, kOledAddr, /*reset=*/true, /*periphBegin=*/false);

  pinMode(PIN_KEYS, INPUT);  // the ladder's own 10 kΩ holds D0 high
  captureOk = captureBegin();
}

void loop() {
  uint32_t nowMs = millis();
  static uint32_t lastKeyMs = 0, lastColumnMs = 0, lastDrawMs = 0;
  if (nowMs - lastKeyMs >= kKeyEveryMs) {
    lastKeyMs = nowMs;
    pollKeys();
  }
  if (nowMs - lastColumnMs >= kColumnMs) {
    lastColumnMs += kColumnMs;
    addColumn();
  }
  // The panel takes ~25 ms of bus time; the capture runs on regardless, in hardware.
  if (oledOk && nowMs - lastDrawMs >= kDrawEveryMs) {
    lastDrawMs = nowMs;
    draw();
  }
}
