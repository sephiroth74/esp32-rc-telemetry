// Whether a byte stream comes from a u-blox receiver at the baud rate it was read at — POST check
// 8, the auto-baud scan of PLAN-ESP32.md §5.2, ROADMAP.md step 3.5.
//
// The scan reads each rate for up to 1.5 s and needs a yes or a no. Bytes read at the wrong rate
// are noise, in which a "0xB5 0x62" or a "$G" turns up by chance; so the evidence here is a whole
// frame with its checksum: a UBX frame (ubx::Scanner), or an NMEA sentence from a GNSS talker,
// "$G…*hh". The module answers with either, whatever it was last configured to send.
//
// Framework-free, like the rest of lib/ubx (§6.1).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ubx.h"

namespace ubx {

class LinkProbe {
 public:
  void push(const uint8_t* data, size_t n);
  bool found() const { return ubx_ || nmea_; }
  bool sawUbx() const { return ubx_; }
  bool sawNmea() const { return nmea_; }

 private:
  void nmeaByte(uint8_t c);

  Scanner scanner_;
  bool ubx_ = false, nmea_ = false;

  // An NMEA sentence in progress: "$", the talker and the fields, "*", two hex digits.
  enum class Nmea : uint8_t { Idle, Body, Hex1, Hex2 };
  Nmea state_ = Nmea::Idle;
  uint8_t length_ = 0;  // characters after the "$"
  uint8_t sum_ = 0;     // XOR of the characters between "$" and "*"
  uint8_t given_ = 0;   // the checksum the sentence states
};

}  // namespace ubx
