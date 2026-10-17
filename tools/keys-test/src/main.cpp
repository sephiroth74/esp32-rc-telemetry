// Key ladder bench test (ROADMAP.md step 1.1) on a Seeed XIAO ESP32-S3 Sense.
//
// Wiring, two-button interim layout (PLAN-ESP32.md §3.1):
//   10 kΩ from D0 to 3V3   OK from D0 to GND   NAV from D0 through 10 kΩ to GND
//   OLED as in tools/oled-sd-test (VCC 3V3, SDA D4, SCL D5)
//
// It reads D0 with the ADC at 100 Hz and classifies every reading with the KEY_MV_BOUNDS of
// firmware/include/config.h. That header and pins.h are the only things it shares with firmware/:
// checking those windows against the real ladder is the point. Per key it keeps the min, average and
// max of the readings seen while the key was held, since boot. They are on screen, and they go to
// the serial monitor every 5 s, ready for docs/hardware.md.
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "pins.h"

enum Key : uint8_t { kOk, kUp, kDown, kLeft, kRight, kNone, kKeyCount };
constexpr Key kNav = Key(KEY_NAV_WINDOW);  // the window NAV reads in, two-button layout

constexpr uint8_t kOledAddr = 0x3C;
constexpr unsigned long kSamplePeriodUs = 1000000UL / KEY_SAMPLE_HZ;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);  // 4-pin modules have no reset line
bool oledOk = false;
bool pullupOk = false;

struct Stats {
  uint32_t count, sum;
  uint16_t min = UINT16_MAX, max;
  unsigned presses;
} stats[kKeyCount];

uint16_t lastMv = 0;
Key candidate = kNone, current = kNone;
uint8_t candidateRun = 0;

const char* keyName(Key k) {
  static const char* const kNames[] = {"OK", "UP", "DOWN", "LEFT", "RIGHT", "none"};
  if (k == kNav && KEY_LAYOUT_DEFAULT == KeyLayout::TwoButtons) return "NAV";
  return kNames[k];
}

Key classify(uint16_t mv) {
  for (uint8_t k = 0; k < kNone; ++k) {
    if (mv < KEY_MV_BOUNDS[k]) return Key(k);
  }
  return kNone;
}

void sample() {
  uint16_t mv = analogReadMilliVolts(PIN_KEYS);
  lastMv = mv;
  Key k = classify(mv);
  candidateRun = k == candidate ? candidateRun + 1 : 1;
  candidate = k;
  if (candidateRun < KEY_CONFIRM_SAMPLES) return;  // a bounce, or a transition between levels

  if (k != current) {
    if (k != kNone) {
      stats[k].presses++;
      Serial.printf("press %s (%u mV)\n", keyName(k), mv);
    } else {
      Serial.printf("release %s\n", keyName(current));
    }
    current = k;
  }
  Stats& s = stats[k];
  s.count++;
  s.sum += mv;
  if (mv < s.min) s.min = mv;
  if (mv > s.max) s.max = mv;
}

// The keys of the fitted layout, plus "none"; any other key only ever shows as the live reading.
const Key kShown[] = {kOk, kNav, kNone};

void printTable() {
  Serial.println("--");
  Serial.printf("pull-up %s, bounds", pullupOk ? "ok" : "MISSING");
  for (uint16_t b : KEY_MV_BOUNDS) Serial.printf(" %u", b);
  Serial.println(" mV");
  for (Key k : kShown) {
    const Stats& s = stats[k];
    if (!s.count) {
      Serial.printf("%-5s not seen yet\n", keyName(k));
      continue;
    }
    Serial.printf("%-5s min %4u avg %4lu max %4u mV, %u presses, %lu samples\n", keyName(k), s.min,
                  (unsigned long)(s.sum / s.count), s.max, s.presses, (unsigned long)s.count);
  }
}

void draw() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  oled.setCursor(0, 0);
  oled.print("Keys: two buttons");
  oled.setCursor(0, 8);
  oled.print(pullupOk ? "10k pull-up ok" : "NO 10k PULL-UP ON D0");
  oled.setCursor(0, 16);
  oled.printf("now %4umV %s", lastMv, keyName(current));
  oled.setCursor(0, 24);
  oled.print("key   min  avg  max");
  int y = 32;
  for (Key k : kShown) {
    const Stats& s = stats[k];
    oled.setCursor(0, y);
    if (s.count) {
      oled.printf("%-4s %4u %4lu %4u", keyName(k), s.min, (unsigned long)(s.sum / s.count), s.max);
    } else {
      oled.printf("%-4s    -", keyName(k));
    }
    y += 8;
  }
  oled.setCursor(0, 56);
  oled.printf("press OK %u  NAV %u", stats[kOk].presses, stats[kNav].presses);
  oled.display();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nKey ladder test (ROADMAP.md step 1.1)");

  // The ADC driver disables the pin's internal pull-up, so with no key pressed only the ladder's own
  // 10 kΩ holds D0 high. Without it the pin floats and reads anywhere.
  pinMode(PIN_KEYS, INPUT);
  uint32_t idle = 0;
  for (int i = 0; i < 16; ++i) {
    idle += analogReadMilliVolts(PIN_KEYS);
    delay(2);
  }
  idle /= 16;
  pullupOk = idle > KEY_MV_BOUNDS[kRight];
  Serial.printf("idle reading %lu mV: %s\n", (unsigned long)idle,
                pullupOk ? "pull-up ok" : "no pull-up — fit 10k from D0 to 3V3");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(kOledAddr);  // begin() succeeds whether or not anything answers
  oledOk = Wire.endTransmission() == 0 &&
           oled.begin(SSD1306_SWITCHCAPVCC, kOledAddr, /*reset=*/true, /*periphBegin=*/false);
  if (!oledOk) Serial.println("no OLED at 0x3C: serial only");
}

void loop() {
  static unsigned long nextSampleUs = micros();
  if ((long)(micros() - nextSampleUs) >= 0) {
    nextSampleUs += kSamplePeriodUs;
    sample();
  }

  static unsigned long lastDrawMs = 0;
  if (oledOk && millis() - lastDrawMs >= 100) {
    lastDrawMs = millis();
    draw();
  }

  static unsigned long lastTableMs = 0;
  if (millis() - lastTableMs >= 5000) {
    lastTableMs = millis();
    printTable();
  }
}
