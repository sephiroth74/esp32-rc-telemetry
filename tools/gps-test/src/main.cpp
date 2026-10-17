// GPS bench test: Flywoo GOKU GM10 Nano (u-blox M10).
//
// Two boards, one environment each in platformio.ini, which sets the pins:
//   seeed_xiao_esp32s3  GPS TX -> D7 (GPIO44)  GPS RX -> D6 (GPIO43)  GPS 5V -> 5V  GND -> GND
//                       — the UART1 pins of firmware/include/pins.h — plus the status on the OLED
//                       of tools/oled-sd-test (SDA D4, SCL D5), if one is connected.
//   esp32dev            GPS TX -> GPIO16 (RX2)  GPS RX -> GPIO17 (TX2)  GPS 5V -> 5V/VIN  GND -> GND
//
// The link itself — baud scan, NMEA and UBX decoding, the MON-VER poll that proves the ESP32 -> GPS
// direction is wired too — lives in tools/lib/gm10, shared with tools/oled-sd-test. This sketch adds
// a status line every second and the serial commands.
//
// Serial monitor commands: r = toggle raw echo, v = poll MON-VER again, b = rescan baud rate.
#include <Arduino.h>
#include <gm10.h>
#ifdef HAS_OLED
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#endif

HardwareSerial gpsPort(GPS_UART);
gm10::Link gps(gpsPort);

void printStatus() {
  const gm10::Fix& fix = gps.fix;
  const gm10::Stats& stats = gps.stats;
  Serial.printf("[%lu %s] %s fix=%s sats=%d lat=%.7f lon=%.7f alt=%.1fm %s=%.2f spd=%.1fkm/h",
                gps.baud, gps.protocol(), fix.time, gps.fixName(), fix.sats, fix.lat, fix.lon,
                fix.alt, fix.fromUbx ? "pdop" : "hdop", fix.dop, fix.speedKmh);
  if (fix.fromUbx) Serial.printf(" hAcc=%.1fm", fix.hAccM);
  Serial.printf(" | nmea %u/%u ubx %u/%u (ok/bad)\n", stats.nmeaOk, stats.nmeaBad, stats.ubxOk,
                stats.ubxBad);
}

// --- OLED, XIAO environment only --------------------------------------------
// Lets the board go outdoors on a power bank, where the fix is, without a laptop.

#ifdef HAS_OLED
constexpr uint8_t kOledAddr = 0x3C;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);  // 4-pin modules have no reset line
bool oledOk = false;

void oledBegin() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  Wire.beginTransmission(kOledAddr);  // begin() succeeds whether or not anything answers
  oledOk = Wire.endTransmission() == 0 &&
           oled.begin(SSD1306_SWITCHCAPVCC, kOledAddr, /*reset=*/true, /*periphBegin=*/false);
  Serial.printf(oledOk ? "OLED at 0x%02X\n" : "no OLED at 0x%02X: serial only\n", kOledAddr);
  if (oledOk) oled.setTextColor(SSD1306_WHITE);
}

// While scanning: a title, and a detail that wraps over the lines below it.
void oledScanning() {
  char detail[80];
  if (gps.noData) {
    snprintf(detail, sizeof detail, "Check 5V, GND and GPS TX -> GPIO%d. Now trying %lu baud",
             GPS_RX_PIN, gps.trying);
  } else {
    snprintf(detail, sizeof detail, "trying %lu baud", gps.trying);
  }
  oled.clearDisplay();
  oled.setTextWrap(true);
  oled.setCursor(0, 0);
  oled.print(gps.noData ? "No GPS data" : "Scanning baud rates");
  oled.setCursor(0, 24);
  oled.print(detail);
  oled.display();
}

// The link and the fix in the top 16 rows (yellow on the bench module), the details below.
void oledStatus() {
  const gm10::Fix& fix = gps.fix;
  const gm10::Stats& stats = gps.stats;
  oled.clearDisplay();
  oled.setTextWrap(false);  // 21 columns: clip rather than spill onto the next line
  oled.setCursor(0, 0);
  oled.printf("%lu %s %s", gps.baud, gps.protocol(true), gps.versionState());
  oled.setCursor(0, 8);
  oled.printf("fix %-5s sats %d", gps.fixName(), fix.sats);
  oled.setCursor(0, 16);
  oled.printf("time %s UTC", fix.time);
  oled.setCursor(0, 24);
  oled.printf("lat %13.7f", fix.lat);
  oled.setCursor(0, 32);
  oled.printf("lon %13.7f", fix.lon);
  oled.setCursor(0, 40);
  oled.printf("alt %.1fm %s %.2f", fix.alt, fix.fromUbx ? "pdop" : "hdop", fix.dop);
  oled.setCursor(0, 48);
  oled.printf("%.1fkm/h", fix.speedKmh);
  if (fix.fromUbx) oled.printf(" hAcc %.1fm", fix.hAccM);
  oled.setCursor(0, 56);
  oled.printf("nmea %u/%u ubx %u/%u", stats.nmeaOk, stats.nmeaBad, stats.ubxOk, stats.ubxBad);
  oled.display();
}

void oledDraw() {
  if (!oledOk) return;
  if (gps.baud) {
    oledStatus();
  } else {
    oledScanning();
  }
}
#else
void oledBegin() {}
void oledDraw() {}
#endif

void handleCommands() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'r':
        gps.rawEcho = !gps.rawEcho;
        Serial.printf("raw echo %s\n", gps.rawEcho ? "on" : "off");
        break;
      case 'v':
        gps.pollVersion();
        break;
      case 'b':
        gps.rescan();
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  gps.begin(GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("\nGM10 GPS test — r: raw echo, v: MON-VER, b: rescan baud");
  oledBegin();
}

void loop() {
  handleCommands();
  gps.poll();

  static unsigned long lastPrintMs = 0;
  if (gps.baud && millis() - lastPrintMs >= 1000) {
    lastPrintMs = millis();
    printStatus();
  }

  // A full frame takes ~25 ms on I2C; at 4 Hz the UART buffer absorbs it even at 230400.
  static unsigned long lastDrawMs = 0;
  if (millis() - lastDrawMs >= 250) {
    lastDrawMs = millis();
    oledDraw();
  }
  delay(1);
}
