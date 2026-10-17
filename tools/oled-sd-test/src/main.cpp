// OLED + microSD + GPS bench demo on a Seeed XIAO ESP32-S3 Sense, with a 0.96" 128x64 SSD1306 I2C
// OLED and a Flywoo GOKU GM10 Nano.
//
// Wiring:  OLED VCC -> 3V3   GND -> GND   SDA -> D4 (GPIO5)   SCL -> D5 (GPIO6)
//          Button between D0 (GPIO1) and GND, no resistor: it uses the internal pull-up.
//          GPS  5V -> 5V     GND -> GND   TX -> D7 (GPIO44)  RX -> D6 (GPIO43)
// Power the OLED from 3V3, not 5V: the module's I2C pull-ups go to its VCC, and the ESP32-S3
// pins are not 5 V tolerant. These are the I2C, button and GNSS pins of firmware/include/pins.h.
//
// The screen alternates every 4 s between the test results below and the live GPS data. The GPS
// link — a baud scan that never blocks, NMEA and UBX decoding — is tools/lib/gm10, shared with
// tools/gps-test, which is the place to go for raw frames and serial commands.
//
// Releasing the button restarts the board and so re-runs every test, e.g. after swapping the card.
// It is a software restart: the chip's EN line is not on any pad, so a hung sketch still needs
// the onboard R button.
//
// On boot it scans the I2C bus, starts the OLED, then mounts the card over SDMMC 1-bit — the bus
// the firmware uses (PLAN-ESP32.md §2.3) — writes, reads back and deletes a file with a long name
// (§7.1), then times a 256 KB write and read-back. The results stay on screen with an uptime
// counter, and go to the serial monitor too.
//
// The card must be FAT32 (or FAT16) on an MBR partition table: the core's FatFs is built with
// exFAT and 64-bit LBA off, so exFAT volumes and GPT disks are not recognised.
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <ff.h>
#include <gm10.h>

constexpr int PIN_I2C_SDA = 5;  // D4
constexpr int PIN_I2C_SCL = 6;  // D5
constexpr int PIN_SD_CLK = 7;   // D8
constexpr int PIN_SD_D0 = 8;    // D9
constexpr int PIN_SD_CMD = 9;   // D10
constexpr int PIN_SD_D3 = 21;   // not on a pad; also the onboard user LED, active low
constexpr int PIN_BUTTON = 1;   // D0, to GND
constexpr int PIN_GPS_TX = 43;  // D6 -> GPS RX
constexpr int PIN_GPS_RX = 44;  // D7 <- GPS TX

constexpr uint8_t kOledAddr = 0x3C;  // a few modules answer on 0x3D: see the I2C scan line
constexpr char kLongPath[] = "/rc-telemetry long filename test.txt";
constexpr char kSpeedPath[] = "/speed-test.bin";

Adafruit_SSD1306 oled(128, 64, &Wire, -1);  // 4-pin modules have no reset line
bool oledOk = false;

HardwareSerial gpsPort(1);  // UART1, as in the firmware (§5.2)
gm10::Link gps(gpsPort);
constexpr unsigned long kPageMs = 4000;

// Result lines, drawn under the title. Rows 0-15 of these modules are yellow, the rest blue.
// The screen has room for five; more only occur when there is no OLED to draw them on.
constexpr int kMaxLines = 8;
char lines[kMaxLines][22];  // 21 columns at text size 1
int lineCount = 0;

void report(const char* fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.println(buf);
  if (lineCount < kMaxLines) snprintf(lines[lineCount++], sizeof lines[0], "%s", buf);
}

bool i2cAnswers(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool scanI2c(const char* label) {
  char found[32] = "";
  for (uint8_t addr = 1; addr < 127; ++addr) {
    if (i2cAnswers(addr)) {
      size_t len = strlen(found);
      snprintf(found + len, sizeof found - len, " %02X", addr);
    }
  }
  report("%s:%s", label, *found ? found : " nothing");
  return *found;
}

// Against the internal pull-down a line reads high only through the module's own pull-up, so
// "ok" means wired and powered. "low" holds even against the internal pull-up: the line is
// clamped by an unpowered module, or shorted. "open" is no module at all, or one without pull-ups.
const char* lineState(int pin) {
  pinMode(pin, INPUT_PULLDOWN);
  delay(2);
  if (digitalRead(pin)) return "ok";
  pinMode(pin, INPUT_PULLUP);
  delay(2);
  return digitalRead(pin) ? "open" : "low";
}

// Run when nothing answers: look at the lines themselves, then retry with SDA and SCL swapped.
void diagnoseI2c() {
  Wire.end();
  report("SDA %s, SCL %s", lineState(PIN_I2C_SDA), lineState(PIN_I2C_SCL));
  Wire.begin(PIN_I2C_SCL, PIN_I2C_SDA);
  scanI2c("swapped");
  Wire.end();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
}

// The log line just before a failure tells the two causes apart: "mount_to_vfs failed" means the
// card answered but holds no FAT volume, a 0x107 timeout means the card never answered at all.
bool mountSd() {
  // DAT3 must be high at init or the card enters SPI mode instead of SD mode (§2.3).
  // Driving it high also keeps the user LED dark.
  pinMode(PIN_SD_D3, OUTPUT);
  digitalWrite(PIN_SD_D3, HIGH);
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  // 40 MHz, the firmware's bus speed. SDMMC_FREQ_DEFAULT (20 MHz) passed bring-up.
  if (!SD_MMC.begin("/sdcard", /*mode1bit=*/true, /*format_if_mount_failed=*/false,
                    SDMMC_FREQ_HIGHSPEED)) {
    report("SD: mount failed");
    return false;
  }
  static const char* const kTypes[] = {"none", "MMC", "SD", "SDHC", "unknown"};
  sdcard_type_t type = SD_MMC.cardType();
  // Cluster size, because a card formatted by the ESP32 itself gets 512 B clusters, which the
  // FAT has to chain one by one: a large slowdown that the write speed below would show.
  // "0:" is the first FatFs volume, which is the card: nothing else is mounted.
  FATFS* vol = nullptr;
  DWORD freeClusters;
  unsigned cluster = f_getfree("0:", &freeClusters, &vol) == FR_OK ? vol->csize * 512u : 0;
  bool kb = cluster >= 1024;
  report("SD: %s %u%s cluster", type <= CARD_UNKNOWN ? kTypes[type] : "?",
         kb ? cluster / 1024 : cluster, kb ? "K" : "B");
  report("%.1fGB, %lluMB used", SD_MMC.cardSize() / 1e9, SD_MMC.usedBytes() / 1000000);
  return true;
}

// Fails at open() if the FAT layer only supports 8.3 names (PLAN-ESP32.md §9, question 12).
void testLongName(fs::FS& fs) {
  static const char kText[] = "XIAO ESP32-S3 Sense long filename test\n";
  File f = fs.open(kLongPath, FILE_WRITE);
  if (!f) {
    report("Long name: no open");
    return;
  }
  size_t written = f.print(kText);
  f.close();
  f = fs.open(kLongPath, FILE_READ);
  String back = f ? f.readString() : String();
  f.close();
  fs.remove(kLongPath);
  report("Long name: %s", written == strlen(kText) && back == kText ? "OK" : "FAIL");
}

// 256 KB in the logger's 32 KB chunks (§4.4), written, read back and compared — the probe of POST
// checks 5 and 6 (§3.3). The long-name file is only a few sectors, too little to expose a bus that
// is marginal at 40 MHz.
void testSpeed(fs::FS& fs) {
  constexpr size_t kChunk = 32 * 1024;
  constexpr int kChunks = 8;
  constexpr unsigned long kBytes = kChunk * kChunks;
  static uint8_t buf[kChunk];
  // Varies per byte, per 256-byte run and per chunk, so a misplaced sector shows up too.
  auto pattern = [](int chunk, size_t i) { return uint8_t(i * 31 + (i >> 8) + chunk * 7); };

  File f = fs.open(kSpeedPath, FILE_WRITE);
  if (!f) {
    report("Speed: no open");
    return;
  }
  bool ok = true;
  unsigned long t0 = millis();
  for (int c = 0; c < kChunks && ok; ++c) {
    for (size_t i = 0; i < kChunk; ++i) buf[i] = pattern(c, i);
    ok = f.write(buf, kChunk) == kChunk;
  }
  f.close();  // inside the timing: the flush is part of the cost
  unsigned long writeMs = millis() - t0;

  f = fs.open(kSpeedPath, FILE_READ);
  t0 = millis();
  for (int c = 0; c < kChunks && ok; ++c) {
    ok = f && f.read(buf, kChunk) == kChunk;
    for (size_t i = 0; i < kChunk && ok; ++i) ok = buf[i] == pattern(c, i);
  }
  unsigned long readMs = millis() - t0;
  f.close();
  fs.remove(kSpeedPath);

  // Bytes per millisecond are KB/s.
  report("W%lu R%lu KB/s %s", kBytes / max(writeMs, 1UL), kBytes / max(readMs, 1UL),
         ok ? "OK" : "BAD");
}

void drawResults() {
  for (int i = 0; i < lineCount; ++i) {
    oled.setCursor(0, 16 + i * 8);
    oled.print(lines[i]);
  }
}

void drawGps() {
  oled.setCursor(0, 16);
  if (!gps.baud) {
    oled.print(gps.noData ? "no data: check wiring" : "scanning baud rates");
    oled.setCursor(0, 24);
    oled.printf("trying %lu baud", gps.trying);
    return;
  }
  const gm10::Fix& fix = gps.fix;
  oled.printf("%lu %s %s", gps.baud, gps.protocol(true), gps.versionState());
  oled.setCursor(0, 24);
  oled.printf("fix %s, %d sats", gps.fixName(), fix.sats);
  oled.setCursor(0, 32);
  oled.printf("lat %13.7f", fix.lat);
  oled.setCursor(0, 40);
  oled.printf("lon %13.7f", fix.lon);
  oled.setCursor(0, 48);
  oled.printf("%s UTC %.0fkm/h", fix.time, fix.speedKmh);
}

void draw() {
  bool gpsPage = millis() / kPageMs % 2;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);  // 21 columns: clip rather than spill onto the next line
  oled.setTextSize(2);      // 16 px tall: exactly fills the yellow band
  oled.setCursor(0, 0);
  oled.print(gpsPage ? "GPS" : "OLED + SD");
  oled.setTextSize(1);
  if (gpsPage) {
    drawGps();
  } else {
    drawResults();
  }
  unsigned long s = millis() / 1000;
  oled.setCursor(0, 56);
  oled.printf("up %02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
  // A dot sweeping along the bottom row proves the screen is refreshing, not frozen.
  int x = 72 + millis() / 40 % 56;
  oled.fillRect(x, 58, 3, 3, SSD1306_WHITE);
  oled.display();
}

void setup() {
  delay(4000);
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);  // USB CDC: give the monitor a moment
  Serial.println("\nOLED + SD test");
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!scanI2c("I2C")) diagnoseI2c();
  // begin() succeeds whether or not anything answers, so probe the address first.
  oledOk = i2cAnswers(kOledAddr) &&
           oled.begin(SSD1306_SWITCHCAPVCC, kOledAddr, /*reset=*/true, /*periphBegin=*/false);
  if (!oledOk) report("OLED: none at %02X", kOledAddr);

  if (mountSd()) {
    testLongName(SD_MMC);
    testSpeed(SD_MMC);
  }
  gps.begin(PIN_GPS_RX, PIN_GPS_TX);
}

void loop() {
  gps.poll();

  // A full frame takes ~25 ms on I2C; at 10 Hz the UART buffer absorbs it even at 230400.
  static unsigned long lastDrawMs = 0;
  if (oledOk && millis() - lastDrawMs >= 100) {
    lastDrawMs = millis();
    draw();
  }

  // On release rather than on press, so a held button does not loop through reboots.
  static bool pressed = false;
  bool down = digitalRead(PIN_BUTTON) == LOW;
  if (pressed && !down) {
    Serial.println("button released: restarting");
    Serial.flush();
    ESP.restart();
  }
  pressed = down;

  // USB CDC drops whatever was printed before the monitor attached, so repeat the results.
  static unsigned long lastDumpMs = 0;
  if (millis() - lastDumpMs >= 5000) {
    lastDumpMs = millis();
    Serial.println("--");
    for (int i = 0; i < lineCount; ++i) Serial.println(lines[i]);
    if (gps.baud) {
      Serial.printf("GPS: %lu %s %s, fix %s, %d sats, %.7f %.7f, %s UTC\n", gps.baud,
                    gps.protocol(), gps.versionState(), gps.fixName(), gps.fix.sats, gps.fix.lat,
                    gps.fix.lon, gps.fix.time);
    } else {
      Serial.printf("GPS: %s, trying %lu baud\n", gps.noData ? "no data" : "scanning", gps.trying);
    }
  }
  delay(2);
}
