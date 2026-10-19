// Which GPIO is the microSD's DAT3 (ROADMAP.md step 1.2) on a Seeed XIAO ESP32-S3 Sense.
//
// Seeed's examples drive GPIO21 as the card's chip select; their wiki lists GPIO3 for DAT3. It
// matters beyond the card: GPIO3 is D2, the throttle input of pins.h, which moves to D1 if GPIO3
// turns out to be DAT3 (PLAN-ESP32.md §2.2).
//
// A card picks its mode at the first CMD0 after power-up: DAT3 high, SD mode; DAT3 low, SPI mode,
// where SD_MMC's init fails. Once it has picked, it ignores DAT3 until its power goes — a first try
// that mounted made every later one mount too. So this tries once per power-up, which only
// unplugging the board gives (a reset leaves the card powered):
//   1st power-up: GPIO3 held low, GPIO21 high, SD_MMC.begin() in 1-bit mode, which leaves DAT3 to
//   us 2nd power-up: GPIO21 held low, GPIO3 high; then the verdict
// The candidate whose low made the card fail is DAT3; the other one, mounting, shows a card is in.
// NVS keeps the step and the first result across the unplugging. Any other reset — the one after
// an upload — tries nothing and asks for the unplugging.
//
// Nothing to wire: GPIO21 is on the Sense board, GPIO3 is the D2 pad, unused on the bench. The OLED
// shows the state too, if it is there; the serial port repeats it every 3 s.
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <esp_system.h>

#include "pins.h"

constexpr uint8_t kCandidates[] = {3, 21};
constexpr uint8_t kOledAddr = 0x3C;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);  // 4-pin modules have no reset line
bool oledOk = false;

char lines[4][32];  // what the serial port repeats and the OLED shows

bool tryMount(uint8_t low, uint32_t& sizeMb) {
  for (uint8_t pin : kCandidates) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, pin == low ? LOW : HIGH);
  }
  delay(10);
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  bool mounted = SD_MMC.begin("/sdcard", /*mode1bit=*/true, /*format_if_mount_failed=*/false,
                              SDMMC_FREQ_DEFAULT);
  sizeMb = mounted ? SD_MMC.cardSize() / (1024 * 1024) : 0;
  SD_MMC.end();
  for (uint8_t pin : kCandidates) pinMode(pin, INPUT);
  return mounted;
}

const char* verdictOf(bool mounted3, bool mounted21) {
  if (!mounted3 && mounted21) return "DAT3 is GPIO3";
  if (mounted3 && !mounted21) return "DAT3 is GPIO21";
  if (mounted3 && mounted21) return "unclear: both ok";
  return "both FAIL: card?";
}

void run() {
  if (esp_reset_reason() != ESP_RST_POWERON) {
    snprintf(lines[0], sizeof lines[0], "Not a power-up:");
    snprintf(lines[1], sizeof lines[1], "unplug the USB and");
    snprintf(lines[2], sizeof lines[2], "plug it back in");
    return;
  }
  Preferences nvs;
  nvs.begin("dat3", /*readOnly=*/false);
  uint8_t step = nvs.getUChar("step", 0);
  uint32_t size = 0;
  if (step == 0) {
    bool mounted = tryMount(3, size);
    nvs.putBool("m3", mounted);
    nvs.putUChar("step", 1);
    snprintf(lines[0], sizeof lines[0], "1/2 GPIO3 low: %s", mounted ? "ok" : "FAIL");
    snprintf(lines[1], sizeof lines[1], "now unplug the USB");
    snprintf(lines[2], sizeof lines[2], "and plug it back in");
  } else {
    bool mounted3 = nvs.getBool("m3", false);
    bool mounted21 = tryMount(21, size);
    nvs.putUChar("step", 0);  // the next power-up starts a new round
    snprintf(lines[0], sizeof lines[0], "GPIO3 low:  %s", mounted3 ? "ok" : "FAIL");
    snprintf(lines[1], sizeof lines[1], "GPIO21 low: %s", mounted21 ? "ok" : "FAIL");
    snprintf(lines[2], sizeof lines[2], "=> %s", verdictOf(mounted3, mounted21));
  }
  if (size) snprintf(lines[3], sizeof lines[3], "card %lu MB", (unsigned long)size);
  nvs.end();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(kOledAddr);  // begin() succeeds whether or not anything answers
  oledOk = Wire.endTransmission() == 0 &&
           oled.begin(SSD1306_SWITCHCAPVCC, kOledAddr, /*reset=*/true, /*periphBegin=*/false);
  run();
  if (oledOk) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextWrap(false);
    oled.setCursor(0, 0);
    oled.print("SD DAT3 test");
    for (int i = 0; i < 4; ++i) {
      oled.setCursor(0, 16 + i * 12);
      oled.print(lines[i]);
    }
    oled.display();
  }
}

void loop() {
  Serial.println("--");
  for (const char* line : lines) {
    if (*line) Serial.println(line);
  }
  delay(3000);
}
