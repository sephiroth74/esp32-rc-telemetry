#include "display.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <string.h>

#include "config.h"
#include "i2c_bus.h"
#include "pins.h"

namespace {

constexpr uint8_t kRows = kStatusLines + kBodyLines;  // one per 8-pixel page
constexpr uint8_t kLastRow = kRows - 1;               // the key hint, the toast and the hold bar
constexpr uint8_t kCellPx = 6;  // a 5x8 font on a 6-pixel grid: 21 columns, one pixel apart
constexpr uint8_t kPageTiles = 128 / 8;
// SSD1306 "Set Normal / Inverse Display".
constexpr uint8_t kCmdNormalDisplay = 0xA6, kCmdInverseDisplay = 0xA7;

// The full-buffer variant: the model is drawn into RAM, and only the dirty pages are sent.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);

bool present = false;  // nothing is ever sent to a display that did not answer: headless boot
char text[kRows][kDisplayCols + 1];
char toast[kDisplayCols + 1];
bool toastShown = false;
uint32_t toastUntilMs = 0;
uint8_t holdPercent = 0;
bool inverted = false;
uint32_t invertUntilMs = 0;
uint8_t dirty = 0;  // one bit per row
uint32_t lastSendMs = 0;
uint32_t fullFrameUs = 0;
uint32_t onePageUs = 0;

// Wrap-safe "a is at or after b" for millisecond counters.
bool reached(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) >= 0; }

void copyLine(char (&dst)[kDisplayCols + 1], const char* src) {
  strncpy(dst, src, kDisplayCols);
  dst[kDisplayCols] = '\0';
}

void setRow(uint8_t row, const char* s) {
  char line[kDisplayCols + 1];
  copyLine(line, s);
  if (strcmp(line, text[row]) == 0) return;
  memcpy(text[row], line, sizeof line);
  dirty |= 1u << row;
}

void drawRow(uint8_t row) {
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, row * 8, 128, 8);
  u8g2.setDrawColor(1);
  const char* s = row == kLastRow && toastShown ? toast : text[row];
  for (uint8_t col = 0; col < kDisplayCols && s[col]; ++col) {
    u8g2.drawGlyph(col * kCellPx, row * 8, s[col]);
  }
  if (row == kLastRow && holdPercent) u8g2.drawHLine(0, 63, holdPercent * 128 / 100);
}

// Times `send`, averaged over a few runs: the figures go into docs/hardware.md.
template <typename F>
uint32_t timeUs(F send) {
  constexpr int kRuns = 4;
  uint32_t start = micros();
  for (int i = 0; i < kRuns; ++i) send();
  return (micros() - start) / kRuns;
}

}  // namespace

bool displayBegin() {
  I2cLock lock;
  Wire.beginTransmission(DISPLAY_I2C_ADDR);
  if (Wire.endTransmission() != 0) return false;

  u8g2.setI2CAddress(DISPLAY_I2C_ADDR * 2);  // U8g2 takes the 8-bit form: the 0x78 of the listings
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setFont(u8g2_font_5x8_mr);
  u8g2.setFontPosTop();

  // The bus budget of §2.4, on a full screen of text so every page carries data.
  for (uint8_t row = 0; row < kRows; ++row) {
    for (uint8_t col = 0; col < kDisplayCols; ++col) u8g2.drawGlyph(col * kCellPx, row * 8, '#');
  }
  fullFrameUs = timeUs([] { u8g2.sendBuffer(); });
  onePageUs = timeUs([] { u8g2.updateDisplayArea(0, 0, kPageTiles, 1); });
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  present = true;
  return true;
}

void displayStatus(uint8_t line, const char* text) {
  if (line < kStatusLines) setRow(line, text);
}

void displayBody(uint8_t line, const char* text) {
  if (line < kBodyLines) setRow(kStatusLines + line, text);
}

void displayToast(const char* text, uint32_t nowMs) {
  copyLine(toast, text);
  toastShown = true;
  toastUntilMs = nowMs + DISPLAY_TOAST_MS;
  dirty |= 1u << kLastRow;
}

void displayHoldBar(uint8_t percent) {
  if (percent == holdPercent) return;
  holdPercent = percent;
  dirty |= 1u << kLastRow;
}

void displayAcknowledge(uint32_t nowMs) {
  if (!present) return;
  {
    I2cLock lock;
    u8g2.sendF("c", kCmdInverseDisplay);
  }
  inverted = true;
  invertUntilMs = nowMs + DISPLAY_ACK_INVERT_MS;
}

void displayRefresh(uint32_t nowMs) {
  if (!present) return;
  if (toastShown && reached(nowMs, toastUntilMs)) {
    toastShown = false;
    dirty |= 1u << kLastRow;
  }
  // The inversion is one command byte, so it ends on time rather than at the next refresh.
  if (inverted && reached(nowMs, invertUntilMs)) {
    I2cLock lock;
    u8g2.sendF("c", kCmdNormalDisplay);
    inverted = false;
  }
  if (!dirty || !reached(nowMs, lastSendMs + DISPLAY_REFRESH_MS)) return;

  for (uint8_t row = 0; row < kRows; ++row) {
    if (dirty & (1u << row)) drawRow(row);
  }
  // Consecutive dirty pages go out as one area: fewer transactions for the same bytes.
  I2cLock lock;
  for (uint8_t row = 0; row < kRows;) {
    if (!(dirty & (1u << row))) {
      ++row;
      continue;
    }
    uint8_t first = row;
    while (row < kRows && (dirty & (1u << row))) ++row;
    u8g2.updateDisplayArea(0, first, kPageTiles, row - first);
  }
  dirty = 0;
  lastSendMs = nowMs;
}

uint32_t displayFullFrameUs() { return fullFrameUs; }

uint32_t displayOnePageUs() { return onePageUs; }
