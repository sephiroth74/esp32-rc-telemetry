#include "nav.h"

namespace ubx {
namespace {

// Little-endian, at a byte offset of the interface description.
uint16_t u16(const uint8_t* p, size_t at) { return p[at] | p[at + 1] << 8; }

uint32_t u32(const uint8_t* p, size_t at) {
  return p[at] | uint32_t(p[at + 1]) << 8 | uint32_t(p[at + 2]) << 16 | uint32_t(p[at + 3]) << 24;
}

int16_t i16(const uint8_t* p, size_t at) { return static_cast<int16_t>(u16(p, at)); }
int32_t i32(const uint8_t* p, size_t at) { return static_cast<int32_t>(u32(p, at)); }

}  // namespace

bool NavPvt::fix3d() const {
  bool threeD = fixType == FixType::Fix3D || fixType == FixType::GnssDeadReckoning;
  return threeD && gnssFixOk() && !invalidLlh();
}

bool decodeNavPvt(const Frame& frame, NavPvt& out) {
  if (frame.cls != kClsNav || frame.id != kIdNavPvt || frame.length != kNavPvtLength) return false;
  const uint8_t* p = frame.payload;
  out.iTowMs = u32(p, 0);
  out.year = u16(p, 4);
  out.month = p[6];
  out.day = p[7];
  out.hour = p[8];
  out.minute = p[9];
  out.second = p[10];
  out.valid = p[11];
  out.tAccNs = u32(p, 12);
  out.nanoNs = i32(p, 16);
  out.fixType = static_cast<FixType>(p[20]);
  out.flags = p[21];
  out.flags2 = p[22];
  out.numSv = p[23];
  out.lonE7 = i32(p, 24);
  out.latE7 = i32(p, 28);
  out.heightMm = i32(p, 32);
  out.hMslMm = i32(p, 36);
  out.hAccMm = u32(p, 40);
  out.vAccMm = u32(p, 44);
  out.velNMmS = i32(p, 48);
  out.velEMmS = i32(p, 52);
  out.velDMmS = i32(p, 56);
  out.gSpeedMmS = i32(p, 60);
  out.headMotE5 = i32(p, 64);
  out.sAccMmS = u32(p, 68);
  out.headAccE5 = u32(p, 72);
  out.pDopE2 = u16(p, 76);
  out.flags3 = u16(p, 78);
  // 80–83 reserved
  out.headVehE5 = i32(p, 84);
  out.magDecE2 = i16(p, 88);
  out.magAccE2 = u16(p, 90);
  return true;
}

NavSatSv NavSat::sv(uint8_t index) const {
  const uint8_t* p = svs + size_t(index) * kNavSatPerSv;
  NavSatSv s;
  s.gnss = static_cast<Gnss>(p[0]);
  s.svId = p[1];
  s.cnoDbHz = p[2];
  s.elevDeg = static_cast<int8_t>(p[3]);
  s.azimDeg = i16(p, 4);
  s.prResDm = i16(p, 6);
  s.flags = u32(p, 8);
  return s;
}

bool decodeNavSat(const Frame& frame, NavSat& out) {
  if (frame.cls != kClsNav || frame.id != kIdNavSat || frame.length < kNavSatHead) return false;
  const uint8_t* p = frame.payload;
  if (p[4] != 1) return false;  // the version this layout is for
  if (frame.length != kNavSatHead + size_t(p[5]) * kNavSatPerSv) return false;
  out.iTowMs = u32(p, 0);
  out.version = p[4];
  out.numSvs = p[5];
  out.svs = p + kNavSatHead;
  return true;
}

}  // namespace ubx
