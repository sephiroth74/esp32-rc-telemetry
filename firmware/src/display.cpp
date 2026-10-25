#include "display.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <string.h>

#include "config.h"
#include "i2c_bus.h"
#include "pins.h"

namespace {

constexpr uint8_t kCellPx = 6;  // both fonts on a 6-pixel grid: 21 columns
constexpr uint8_t kPageTiles = 128 / 8;
// SSD1306 "Set Normal / Inverse Display".
constexpr uint8_t kCmdNormalDisplay = 0xA6, kCmdInverseDisplay = 0xA7;

// Where each line of a ScreenText goes, and in which font. The small grid is a line per page; the
// large one puts its status line in the 16-pixel yellow band, three 13-pixel body rows under it,
// and the hint in the 5x8 on the last page.
struct Row {
  uint8_t y, height;
  const uint8_t* font;
  uint8_t textY;  // where the glyphs start, within or at the row
};
const Row kSmallRows[] = {
    {0, 8, u8g2_font_5x8_mr, 0},   {8, 8, u8g2_font_5x8_mr, 8},   {16, 8, u8g2_font_5x8_mr, 16},
    {24, 8, u8g2_font_5x8_mr, 24}, {32, 8, u8g2_font_5x8_mr, 32}, {40, 8, u8g2_font_5x8_mr, 40},
    {48, 8, u8g2_font_5x8_mr, 48}, {56, 8, u8g2_font_5x8_mr, 56},
};
const Row kLargeRows[] = {
    {0, 16, u8g2_font_6x13B_mr, 1},   {16, 13, u8g2_font_6x13B_mr, 16},
    {29, 13, u8g2_font_6x13B_mr, 29}, {42, 14, u8g2_font_6x13B_mr, 42},
    {56, 8, u8g2_font_5x8_mr, 56},
};
// The body's pixel rows on both grids, where the scroll bar runs: under the yellow band, above the
// hint.
constexpr uint8_t kBodyTop = 16, kBodyEnd = 56;

// The full-buffer variant: the model is drawn into RAM, and only the dirty pages are sent.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);

bool present = false;  // nothing is ever sent to a display that did not answer: headless boot
ui::TextSize textSize = ui::TextSize::Large;
char text[ui::kMaxScreenLines][ui::kScreenCols + 1];
uint8_t rowCount = ui::screenLines(ui::TextSize::Large);
uint8_t scrollTotal = 0, scrollFirst = 0;
char toast[ui::kScreenCols + 1];
bool toastShown = false;
uint32_t toastUntilMs = 0;
uint8_t holdPercent = 0;
bool inverted = false;
int16_t contrast = -1;  // as last sent; -1 before the first
uint32_t invertUntilMs = 0;
uint8_t dirtyRows = 0;  // one bit per row of the current grid
bool scrollDirty = false;
uint32_t lastSendMs = 0;
uint32_t fullFrameUs = 0;
uint32_t onePageUs = 0;

// Wrap-safe "a is at or after b" for millisecond counters.
bool reached(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) >= 0; }

const Row* rows() { return textSize == ui::TextSize::Small ? kSmallRows : kLargeRows; }
uint8_t toastRow() { return textSize == ui::TextSize::Small ? rowCount - 1 : rowCount - 2; }

uint8_t pagesOf(const Row& r) {
  uint8_t first = r.y / 8, last = (r.y + r.height - 1) / 8, mask = 0;
  for (uint8_t p = first; p <= last; ++p) mask |= 1u << p;
  return mask;
}

uint8_t bodyPages() {
  uint8_t mask = 0;
  for (uint8_t p = kBodyTop / 8; p < kBodyEnd / 8; ++p) mask |= 1u << p;
  return mask;
}

void drawRow(uint8_t index) {
  const Row& r = rows()[index];
  const char* s = index == toastRow() && toastShown ? toast : text[index];
  u8g2.setFont(r.font);
  for (uint8_t col = 0; col < ui::kScreenCols && s[col]; ++col) {
    u8g2.drawGlyph(col * kCellPx, r.textY, s[col]);
  }
}

// Two pixels on the right edge, which 21 columns of 6 pixels leave free.
void drawScrollBar() {
  if (scrollTotal == 0) return;
  uint8_t span = kBodyEnd - kBodyTop;
  uint8_t visible = ui::bodyRows(textSize);
  uint8_t top = kBodyTop + span * scrollFirst / scrollTotal;
  uint8_t height = span * visible / scrollTotal;
  if (height < 3) height = 3;
  u8g2.drawBox(126, top, 2, height);
}

void markRow(uint8_t index) { dirtyRows |= 1u << index; }

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
  u8g2.setFontPosTop();

  // The bus budget of §2.4, on a full screen of text so every page carries data.
  u8g2.setFont(u8g2_font_5x8_mr);
  for (uint8_t row = 0; row < 8; ++row) {
    for (uint8_t col = 0; col < ui::kScreenCols; ++col) {
      u8g2.drawGlyph(col * kCellPx, row * 8, '#');
    }
  }
  fullFrameUs = timeUs([] { u8g2.sendBuffer(); });
  onePageUs = timeUs([] { u8g2.updateDisplayArea(0, 0, kPageTiles, 1); });
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  present = true;
  return true;
}

void displayOff() {
  if (!present) return;
  I2cLock lock;
  u8g2.setPowerSave(1);
}

void displaySetTextSize(ui::TextSize size) {
  if (size == textSize) return;
  textSize = size;
  rowCount = ui::screenLines(size);
  for (auto& line : text) line[0] = '\0';
  dirtyRows = (1u << rowCount) - 1;  // the rows cover every page: the whole panel is redrawn
  scrollDirty = true;
}

void displaySetContrast(uint8_t brightness, bool dimmed) {
  constexpr uint8_t kLevels = sizeof DISPLAY_CONTRAST / sizeof DISPLAY_CONTRAST[0];
  uint8_t value = dimmed ? DISPLAY_CONTRAST_DIMMED : DISPLAY_CONTRAST[brightness % kLevels];
  if (!present || value == contrast) return;
  I2cLock lock;
  u8g2.setContrast(value);
  contrast = value;
}

void displayShow(const ui::ScreenText& screen) {
  for (uint8_t i = 0; i < rowCount && i < screen.count; ++i) {
    if (strcmp(text[i], screen.lines[i]) == 0) continue;
    memcpy(text[i], screen.lines[i], sizeof text[i]);
    markRow(i);
  }
  if (screen.scrollTotal != scrollTotal || screen.scrollFirst != scrollFirst) {
    scrollTotal = screen.scrollTotal;
    scrollFirst = screen.scrollFirst;
    scrollDirty = true;
  }
}

void displayToast(const char* message, uint32_t nowMs) {
  strncpy(toast, message, ui::kScreenCols);
  toast[ui::kScreenCols] = '\0';
  toastShown = true;
  toastUntilMs = nowMs + DISPLAY_TOAST_MS;
  markRow(toastRow());
}

void displayHoldBar(uint8_t percent) {
  if (percent == holdPercent) return;
  holdPercent = percent;
  markRow(rowCount - 1);  // the bottom pixel row is in the hint's row on both grids
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

namespace {

void refresh(uint32_t nowMs, bool now) {
  if (!present) return;
  if (toastShown && reached(nowMs, toastUntilMs)) {
    toastShown = false;
    markRow(toastRow());
  }
  // The inversion is one command byte, so it ends on time rather than at the next refresh.
  if (inverted && reached(nowMs, invertUntilMs)) {
    I2cLock lock;
    u8g2.sendF("c", kCmdNormalDisplay);
    inverted = false;
  }
  if (!dirtyRows && !scrollDirty) return;
  if (!now && !reached(nowMs, lastSendMs + DISPLAY_REFRESH_MS)) return;

  // A large row can straddle two pages, so work in pages: clear the dirty ones, then redraw every
  // row that touches them, whether that row changed or not.
  const Row* grid = rows();
  uint8_t pages = scrollDirty ? bodyPages() : 0;
  for (uint8_t i = 0; i < rowCount; ++i) {
    if (dirtyRows & (1u << i)) pages |= pagesOf(grid[i]);
  }
  u8g2.setDrawColor(0);
  for (uint8_t p = 0; p < 8; ++p) {
    if (pages & (1u << p)) u8g2.drawBox(0, p * 8, 128, 8);
  }
  u8g2.setDrawColor(1);
  for (uint8_t i = 0; i < rowCount; ++i) {
    if (pagesOf(grid[i]) & pages) drawRow(i);
  }
  if (pages & bodyPages()) drawScrollBar();
  if ((pages & 0x80) && holdPercent) u8g2.drawHLine(0, 63, holdPercent * 128 / 100);

  // Consecutive dirty pages go out as one area: fewer transactions for the same bytes.
  I2cLock lock;
  for (uint8_t p = 0; p < 8;) {
    if (!(pages & (1u << p))) {
      ++p;
      continue;
    }
    uint8_t first = p;
    while (p < 8 && (pages & (1u << p))) ++p;
    u8g2.updateDisplayArea(0, first, kPageTiles, p - first);
  }
  dirtyRows = 0;
  scrollDirty = false;
  lastSendMs = nowMs;
}

}  // namespace

void displayRefresh(uint32_t nowMs) { refresh(nowMs, /*now=*/false); }

void displayFlush(uint32_t nowMs) { refresh(nowMs, /*now=*/true); }

uint32_t displayFullFrameUs() { return fullFrameUs; }

uint32_t displayOnePageUs() { return onePageUs; }
