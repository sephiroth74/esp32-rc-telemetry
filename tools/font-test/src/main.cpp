// Font comparison on the bench OLED (PLAN-ESP32.md §3.2): the same menu texts in each candidate
// font, to pick the one that is readable at arm's length on the 0.96" panel.
//
// Wiring as in tools/keys-test: OLED on D4/D5, OK and NAV on the D0 ladder.
// NAV shows the next font, OK the previous one. The yellow band names the font and its grid; the
// body shows as many sample lines as the font leaves room for, cut at its column count.
#include <Arduino.h>
#include <U8g2lib.h>

#include "config.h"
#include "pins.h"

struct Candidate {
  const char* name;
  const uint8_t* font;
  uint8_t cellPx;  // character pitch: the glyphs drawn one by one on this grid
  uint8_t rowPx;   // line pitch
};

// Smallest to largest. The 5x8 is what the firmware uses now.
const Candidate kFonts[] = {
    {"5x8", u8g2_font_5x8_mr, 6, 8},          {"6x12", u8g2_font_6x12_mr, 6, 12},
    {"6x13B", u8g2_font_6x13B_mr, 6, 13},     {"7x13B", u8g2_font_7x13B_mr, 7, 13},
    {"8x13B", u8g2_font_8x13B_mr, 8, 13},     {"spleen8x16", u8g2_font_spleen8x16_mr, 8, 16},
    {"9x15B", u8g2_font_9x15B_mr, 9, 16},
};
constexpr int kFontCount = sizeof kFonts / sizeof kFonts[0];

const char* const kSample[] = {
    "Track N4512E00934", "Start line: set", "Best  0:58.902",
    "Last  1:02.341 L12", "> Settings", "OK menu  hold: start",
};
constexpr int kSampleCount = sizeof kSample / sizeof kSample[0];

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
int current = 0;

void drawText(const Candidate& c, int y, const char* s) {
  int cols = 128 / c.cellPx;
  for (int col = 0; col < cols && s[col]; ++col) u8g2.drawGlyph(col * c.cellPx, y, s[col]);
}

void draw() {
  const Candidate& c = kFonts[current];
  u8g2.clearBuffer();
  u8g2.setFont(c.font);
  u8g2.setFontPosTop();
  char label[32];
  snprintf(label, sizeof label, "%d/%d %s %dc", current + 1, kFontCount, c.name, 128 / c.cellPx);
  drawText(c, 0, label);  // the yellow band, rows 0–15
  int line = 0;
  for (int y = 16; y + c.rowPx <= 64 && line < kSampleCount; y += c.rowPx) {
    drawText(c, y, kSample[line++]);
  }
  u8g2.sendBuffer();
  Serial.printf("font %d/%d: %s, %d columns, %d body lines\n", current + 1, kFontCount, c.name,
                128 / c.cellPx, line);
}

// The ladder, read as in the firmware: two equal readings in a row make a key.
int readKey() {
  uint16_t mv = analogReadMilliVolts(PIN_KEYS);
  for (int k = 0; k < 5; ++k) {
    if (mv < KEY_MV_BOUNDS[k]) return k;
  }
  return 5;  // none
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_KEYS, INPUT);
  u8g2.setI2CAddress(DISPLAY_I2C_ADDR * 2);
  u8g2.setBusClock(400000);
  u8g2.begin();
  draw();
}

void loop() {
  static int last = 5, candidate = 5, run = 0;
  int key = readKey();
  run = key == candidate ? run + 1 : 1;
  candidate = key;
  if (run == KEY_CONFIRM_SAMPLES && key != last) {
    if (key == KEY_NAV_WINDOW) current = (current + 1) % kFontCount;
    if (key == 0) current = (current + kFontCount - 1) % kFontCount;  // OK
    if (key != 5) draw();
    last = key;
  }
  delay(10);
}
