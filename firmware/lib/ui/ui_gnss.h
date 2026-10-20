// GNSS acquisition as the GNSS screen tells it — PLAN-ESP32.md §3.4, §3.5, ROADMAP.md step 4.4.
//
// An acquisition starts at boot, when OK is held on the GNSS screen, and when a fix is lost. It
// reaches Fix once a 3D fix with hAcc within fixHAccMm has held for fixHoldMs: the fix the menu's
// gating and the laps trust. Until then it shows Acquiring, and Timeout after timeoutMs, still
// waiting — a fix later still counts. A fix is lost when an epoch is no 3D fix any more, or when
// none has come for staleMs. The accuracy that admits a fix does not end one, so a fix hovering
// about the limit does not flap.
//
// Framework-free (§6.1): the epochs and the clock come from the caller.
#pragma once

#include <stdint.h>

namespace ui {

struct AcquisitionTiming {
  uint32_t fixHAccMm = 5000;
  uint32_t fixHoldMs = 3000;
  uint32_t timeoutMs = 90000;
  uint32_t staleMs = 2000;
};

enum class AcqState : uint8_t { Acquiring, Fix, Timeout };

class Acquisition {
 public:
  explicit Acquisition(const AcquisitionTiming& timing) : t_(timing) {}

  void restart(uint32_t nowMs);
  // Each NAV-PVT: whether it is a 3D fix the module vouches for (ubx::NavPvt::fix3d), and its hAcc.
  void epoch(bool fix3d, uint32_t hAccMm, uint32_t nowMs);
  void tick(uint32_t nowMs);  // notices epochs that stopped coming

  AcqState state(uint32_t nowMs) const;
  bool fix() const { return fix_; }
  uint32_t elapsedS(uint32_t nowMs) const { return (nowMs - startMs_) / 1000; }
  uint32_t ttffS() const { return ttffMs_ / 1000; }  // what the fix took; meaningful with fix()

 private:
  void lose(uint32_t nowMs);

  AcquisitionTiming t_;
  uint32_t startMs_ = 0;
  bool holding_ = false;  // qualifying epochs since holdFromMs_
  uint32_t holdFromMs_ = 0;
  bool fix_ = false;
  uint32_t ttffMs_ = 0;
  bool anyEpoch_ = false;
  uint32_t lastEpochMs_ = 0;
};

}  // namespace ui
