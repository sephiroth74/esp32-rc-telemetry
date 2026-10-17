#include "gm10.h"

namespace gm10 {
namespace {

// The u-blox factory default is 9600; Betaflight and Flywoo usually leave it at 115200.
constexpr unsigned long kBauds[] = {115200, 9600, 38400, 57600, 230400};
constexpr size_t kBaudCount = sizeof kBauds / sizeof kBauds[0];
constexpr unsigned long kScanWindowMs = 1500;
constexpr unsigned long kSilenceRescanMs = 5000;
constexpr unsigned long kVersionTimeoutMs = 2000;

template <typename T>
T le(const uint8_t* p) {  // the ESP32 is little-endian, like UBX
  T v;
  memcpy(&v, p, sizeof v);
  return v;
}

bool nmeaChecksumOk(const char* s) {
  const char* star = strchr(s, '*');
  if (!star || strlen(star) != 3) return false;
  uint8_t sum = 0;
  for (const char* p = s; p < star; ++p) sum ^= *p;
  return sum == strtoul(star + 1, nullptr, 16);
}

double nmeaDeg(const char* v, const char* hemi) {  // ddmm.mmmm -> decimal degrees
  if (!*v) return 0;
  double x = atof(v);
  double deg = floor(x / 100) + fmod(x, 100) / 60;
  return (*hemi == 'S' || *hemi == 'W') ? -deg : deg;
}

}  // namespace

// --- Link and baud scan -------------------------------------------------------

void Link::begin(int rxPin, int txPin) {
  rxPin_ = rxPin;
  txPin_ = txPin;
  // Must precede begin(). 2 KB absorbs a ~25 ms OLED frame even at 230400 baud.
  port_.setRxBufferSize(2048);
  port_.begin(kBauds[0], SERIAL_8N1, rxPin, txPin);
  rescan();
}

void Link::rescan() {
  baud = 0;
  tryIndex_ = 0;
  tryBaud();
}

void Link::tryBaud() {
  trying = kBauds[tryIndex_];
  port_.updateBaudRate(trying);
  delay(20);
  while (port_.available()) port_.read();
  stats = {};
  inLine_ = false;
  ubx_.state = kSync1;
  tryStartMs_ = millis();
}

void Link::poll() {
  while (port_.available()) feed(port_.read());
  unsigned long now = millis();

  if (!baud) {
    if (stats.nmeaOk + stats.ubxOk >= 2) {
      baud = trying;
      noData = false;
      Serial.printf("GM10: %lu baud OK (%u NMEA, %u UBX)\n", baud, stats.nmeaOk, stats.ubxOk);
      pollVersion();
    } else if (now - tryStartMs_ >= kScanWindowMs) {
      Serial.printf("GM10: nothing at %lu baud (%u bad frames)\n", trying,
                    stats.nmeaBad + stats.ubxBad);
      if (++tryIndex_ == kBaudCount) {
        tryIndex_ = 0;
        noData = true;
        Serial.printf("GM10: no data at any baud — check 5V/GND, GPS TX -> GPIO%d, GPS LED on?\n",
                      rxPin_);
      }
      tryBaud();
    }
    return;
  }

  if (verPollMs_ && !verSeen && now - verPollMs_ > kVersionTimeoutMs) {
    Serial.printf("GM10: no MON-VER reply — check GPS RX <- GPIO%d (RX works, TX does not)\n",
                  txPin_);
    verPollMs_ = 0;
  }
  if (now - lastValidMs_ > kSilenceRescanMs) {
    Serial.println("GM10: no valid data for 5 s, rescanning");
    rescan();
  }
}

void Link::pollVersion() {
  static const uint8_t kPollMonVer[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
  port_.write(kPollMonVer, sizeof kPollMonVer);
  verSeen = false;
  verPollMs_ = millis();
}

const char* Link::fixName() const {
  if (fix.quality < 0) return "-";
  if (fix.fromUbx) {
    static const char* const kNames[] = {"none", "DR", "2D", "3D", "GNSS+DR", "time"};
    return fix.quality <= 5 ? kNames[fix.quality] : "?";
  }
  switch (fix.quality) {
    case 0: return "none";
    case 1: return "GPS";
    case 2: return "DGPS";
    case 6: return "DR";
    default: return "other";
  }
}

const char* Link::protocol(bool brief) const {
  if (stats.ubxOk && stats.nmeaOk) return brief ? "BOTH" : "NMEA+UBX";
  return stats.ubxOk ? "UBX" : "NMEA";
}

const char* Link::versionState() const {
  return verSeen ? "ver ok" : verPollMs_ ? "ver ..." : "ver none";
}

void Link::feed(uint8_t c) {
  nmeaFeed(c);
  ubxFeed(c);
}

// --- NMEA ---------------------------------------------------------------------

void Link::handleNmea(char* s) {  // s is the sentence without the leading '$'
  char* f[24];
  int n = 0;
  f[n++] = s;
  for (char* p = s; *p && n < 24; ++p) {
    if (*p == ',' || *p == '*') {
      *p = '\0';
      f[n++] = p + 1;
    }
  }
  if (strlen(f[0]) != 5) return;  // skips proprietary sentences such as PUBX
  const char* type = f[0] + 2;    // drop the talker ID (GP, GN, GL, GA, GB)

  if (strcmp(type, "GGA") == 0 && n > 9) {
    fix.fromUbx = false;
    if (strlen(f[1]) >= 6) {
      snprintf(fix.time, sizeof fix.time, "%.2s:%.2s:%.2s", f[1], f[1] + 2, f[1] + 4);
    }
    fix.lat = nmeaDeg(f[2], f[3]);
    fix.lon = nmeaDeg(f[4], f[5]);
    fix.quality = atoi(f[6]);
    fix.sats = atoi(f[7]);
    fix.dop = atof(f[8]);
    fix.alt = atof(f[9]);
  } else if (strcmp(type, "RMC") == 0 && n > 7) {
    fix.speedKmh = atof(f[7]) * 1.852f;  // knots
  }
}

void Link::nmeaFeed(uint8_t c) {
  if (c == '$') {
    inLine_ = true;
    lineLen_ = 0;
    return;
  }
  if (!inLine_) return;
  if (c == '\r' || c == '\n') {
    inLine_ = false;
    line_[lineLen_] = '\0';
    if (!nmeaChecksumOk(line_)) {
      stats.nmeaBad++;
      return;
    }
    stats.nmeaOk++;
    lastValidMs_ = millis();
    if (rawEcho) Serial.printf("$%s\n", line_);
    handleNmea(line_);
    return;
  }
  if (c < 0x20 || c > 0x7E || lineLen_ >= sizeof line_ - 1) {
    inLine_ = false;  // binary data or overlong line: not NMEA
    return;
  }
  line_[lineLen_++] = c;
}

// --- UBX ----------------------------------------------------------------------

void Link::handleUbx() {
  const uint8_t* p = ubx_.payload;
  if (rawEcho) Serial.printf("UBX %02X-%02X len=%u\n", ubx_.cls, ubx_.id, ubx_.len);

  if (ubx_.cls == 0x01 && ubx_.id == 0x07 && ubx_.len == 92) {  // NAV-PVT
    fix.fromUbx = true;
    snprintf(fix.time, sizeof fix.time, "%02u:%02u:%02u", p[8], p[9], p[10]);
    fix.quality = p[20];
    fix.sats = p[23];
    fix.lon = le<int32_t>(p + 24) * 1e-7;
    fix.lat = le<int32_t>(p + 28) * 1e-7;
    fix.alt = le<int32_t>(p + 36) / 1000.0f;       // hMSL, mm
    fix.hAccM = le<uint32_t>(p + 40) / 1000.0f;    // mm
    fix.speedKmh = le<int32_t>(p + 60) * 0.0036f;  // gSpeed, mm/s
    fix.dop = le<uint16_t>(p + 76) * 0.01f;        // pDOP: PVT carries no HDOP
  } else if (ubx_.cls == 0x0A && ubx_.id == 0x04 && ubx_.len >= 40) {  // MON-VER
    verSeen = true;
    const char* s = reinterpret_cast<const char*>(p);
    Serial.printf("MON-VER sw=\"%.30s\" hw=\"%.10s\"\n", s, s + 30);
    for (uint16_t off = 40; off + 30 <= ubx_.len; off += 30) Serial.printf("  %.30s\n", s + off);
  }
}

void Link::ubxAdd(uint8_t c) {
  ubx_.ckA += c;
  ubx_.ckB += ubx_.ckA;
}

void Link::ubxFeed(uint8_t c) {
  switch (ubx_.state) {
    case kSync1:
      if (c == 0xB5) ubx_.state = kSync2;
      break;
    case kSync2:
      ubx_.state = c == 0x62 ? kClass : c == 0xB5 ? kSync2 : kSync1;
      ubx_.ckA = ubx_.ckB = 0;
      break;
    case kClass:
      ubx_.cls = c;
      ubxAdd(c);
      ubx_.state = kId;
      break;
    case kId:
      ubx_.id = c;
      ubxAdd(c);
      ubx_.state = kLen1;
      break;
    case kLen1:
      ubx_.len = c;
      ubxAdd(c);
      ubx_.state = kLen2;
      break;
    case kLen2:
      ubx_.len |= c << 8;
      ubxAdd(c);
      ubx_.pos = 0;
      if (ubx_.len > sizeof ubx_.payload) {
        stats.ubxBad++;
        ubx_.state = kSync1;
      } else {
        ubx_.state = ubx_.len ? kPayload : kCkA;
      }
      break;
    case kPayload:
      ubx_.payload[ubx_.pos++] = c;
      ubxAdd(c);
      if (ubx_.pos == ubx_.len) ubx_.state = kCkA;
      break;
    case kCkA:
      if (c == ubx_.ckA) {
        ubx_.state = kCkB;
      } else {
        stats.ubxBad++;
        ubx_.state = kSync1;
      }
      break;
    case kCkB:
      ubx_.state = kSync1;
      if (c != ubx_.ckB) {
        stats.ubxBad++;
        break;
      }
      stats.ubxOk++;
      lastValidMs_ = millis();
      handleUbx();
      break;
  }
}

}  // namespace gm10
