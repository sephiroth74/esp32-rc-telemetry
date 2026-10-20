// NAV-PVT and NAV-SAT decode — PLAN-ESP32.md §5.2, §5.3, ROADMAP.md step 4.2.
//
// A convenience layer, never the source of truth (§5.3): the frames are logged as they came, and
// this reads fields out of them for whoever needs a few — the GNSS screen, the status bar, the
// automatic start, the laps. NAV-PVT comes out whole, every field of its 92 bytes in the units of
// the interface description (M10 SPG 5.10, protocol 34.10), so nothing needs a second decoder later.
// NAV-SAT is read in place, a satellite at a time, since its length depends on the sky.
//
// Framework-free, like the rest of lib/ubx (§6.1).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ubx.h"

namespace ubx {

constexpr uint8_t kClsNav = 0x01, kIdNavPvt = 0x07, kIdNavSat = 0x35;
constexpr uint16_t kNavPvtLength = 92;
constexpr uint16_t kNavSatHead = 8, kNavSatPerSv = 12;

// NAV-PVT's fixType.
enum class FixType : uint8_t {
  None = 0,
  DeadReckoning = 1,
  Fix2D = 2,  // no altitude, from three satellites: not trusted for laps (§3.4)
  Fix3D = 3,
  GnssDeadReckoning = 4,
  TimeOnly = 5,
};

struct NavPvt {
  uint32_t iTowMs;  // GPS time of week of the epoch: what ties PVT, SAT and the rest together
  uint16_t year;
  uint8_t month, day, hour, minute, second;
  uint8_t valid;  // bit 0 date, 1 time, 2 fully resolved, 3 magnetic declination
  uint32_t tAccNs;
  int32_t nanoNs;  // fraction of the second, -1e9 to 1e9
  FixType fixType;
  uint8_t flags;  // bit 0 gnssFixOK, 1 diffSoln, 2–4 psmState, 5 headVehValid, 6–7 carrSoln
  uint8_t flags2;
  uint8_t numSv;  // used in the solution
  int32_t lonE7, latE7;
  int32_t heightMm;  // above the ellipsoid
  int32_t hMslMm;    // above mean sea level
  uint32_t hAccMm, vAccMm;
  int32_t velNMmS, velEMmS, velDMmS;
  int32_t gSpeedMmS;  // ground speed, from Doppler: better than differentiating positions (§5.2)
  int32_t headMotE5;  // heading of motion, 1e-5 degrees
  uint32_t sAccMmS;
  uint32_t headAccE5;
  uint16_t pDopE2;  // 0.01
  uint16_t flags3;  // bit 0 invalidLlh, 1–4 lastCorrectionAge
  int32_t headVehE5;
  int16_t magDecE2;
  uint16_t magAccE2;

  bool gnssFixOk() const { return flags & 0x01; }
  bool diffSoln() const { return flags & 0x02; }  // SBAS corrections applied
  bool validDate() const { return valid & 0x01; }
  bool validTime() const { return valid & 0x02; }
  bool invalidLlh() const { return flags3 & 0x01; }
  // A 3D fix the module itself vouches for, with a position that is not flagged invalid: the only
  // kind the GNSS screen calls a fix and the laps use (§3.4).
  bool fix3d() const;
};

// False for anything but a NAV-PVT of 92 bytes.
bool decodeNavPvt(const Frame& frame, NavPvt& out);

// The constellations of NAV-SAT's and the configuration's gnssId.
enum class Gnss : uint8_t { Gps = 0, Sbas = 1, Galileo = 2, Beidou = 3, Imes = 4, Qzss = 5, Glonass = 6 };

struct NavSatSv {
  Gnss gnss;
  uint8_t svId;
  uint8_t cnoDbHz;  // carrier to noise: 0 for a satellite not tracked
  int8_t elevDeg;   // -90 to 90; out of range when unknown
  int16_t azimDeg;  // 0 to 360
  int16_t prResDm;  // pseudorange residual, 0.1 m
  uint32_t flags;

  uint8_t quality() const { return flags & 0x07; }  // 0 no signal … 4 code locked … 5–7 carrier
  bool used() const { return flags & 0x08; }        // in the navigation solution
  uint8_t health() const { return (flags >> 4) & 0x03; }  // 0 unknown, 1 healthy, 2 unhealthy
  bool diffCorr() const { return flags & 0x40; }
  bool sbasCorrUsed() const { return flags & (1u << 16); }
};

struct NavSat {
  uint32_t iTowMs;
  uint8_t version;
  uint8_t numSvs;
  const uint8_t* svs;  // into the frame: valid as long as its bytes are

  NavSatSv sv(uint8_t index) const;  // index < numSvs
};

// False for anything but a NAV-SAT of version 1 whose length matches its satellite count.
bool decodeNavSat(const Frame& frame, NavSat& out);

}  // namespace ubx
