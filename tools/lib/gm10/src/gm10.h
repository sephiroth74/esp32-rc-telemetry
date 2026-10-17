// Bench-only link to a Flywoo GOKU GM10 Nano (u-blox M10), shared by tools/gps-test and
// tools/oled-sd-test. It scans the common baud rates until valid frames arrive, then decodes NMEA
// (GGA/RMC) and UBX (NAV-PVT, MON-VER), so it works whatever the module was last configured to
// output. The firmware does not use it: it has its own UBX-only parser (PLAN-ESP32.md §5.4).
//
// Nothing blocks: poll() drains the UART and advances the baud scan by the clock, so a sketch can
// keep drawing and reading its button while the scan runs.
#pragma once

#include <Arduino.h>

namespace gm10 {

struct Stats {
  unsigned nmeaOk, nmeaBad, ubxOk, ubxBad;
};

struct Fix {
  char time[12] = "--:--:--";
  int quality = -1;  // GGA fix quality for NMEA, fixType for UBX
  int sats = 0;
  double lat = 0, lon = 0;
  float alt = 0, dop = 0, speedKmh = 0, hAccM = 0;
  bool fromUbx = false;
};

class Link {
 public:
  explicit Link(HardwareSerial& port) : port_(port) {}

  void begin(int rxPin, int txPin);  // rxPin <- GPS TX, txPin -> GPS RX
  void poll();                       // call as often as possible
  void rescan();
  void pollVersion();  // UBX-MON-VER: a reply proves the ESP32 -> GPS direction is wired too

  const char* fixName() const;
  const char* protocol(bool brief = false) const;  // brief fits the 21 OLED columns
  const char* versionState() const;                 // "ver ok", "ver ..." or "ver none"

  Fix fix;
  Stats stats{};
  unsigned long baud = 0;    // 0 = not locked, scanning
  unsigned long trying = 0;  // the baud rate under test while scanning
  bool noData = false;       // a whole scan pass found nothing
  bool verSeen = false;
  bool rawEcho = false;      // print every valid frame to Serial

 private:
  void tryBaud();
  void feed(uint8_t c);
  void nmeaFeed(uint8_t c);
  void handleNmea(char* s);
  void ubxFeed(uint8_t c);
  void ubxAdd(uint8_t c);
  void handleUbx();

  enum UbxState : uint8_t { kSync1, kSync2, kClass, kId, kLen1, kLen2, kPayload, kCkA, kCkB };

  HardwareSerial& port_;
  int rxPin_ = -1, txPin_ = -1;
  size_t tryIndex_ = 0;
  unsigned long tryStartMs_ = 0;
  unsigned long lastValidMs_ = 0;
  unsigned long verPollMs_ = 0;

  char line_[128];
  size_t lineLen_ = 0;
  bool inLine_ = false;

  struct {
    UbxState state = kSync1;
    uint8_t cls, id, ckA, ckB;
    uint16_t len, pos;
    uint8_t payload[512];
  } ubx_;
};

}  // namespace gm10
