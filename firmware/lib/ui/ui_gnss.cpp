#include "ui_gnss.h"

namespace ui {

void Acquisition::restart(uint32_t nowMs) {
  startMs_ = nowMs;
  holding_ = false;
  fix_ = false;
  ttffMs_ = 0;
}

void Acquisition::lose(uint32_t nowMs) {
  restart(nowMs);  // a new acquisition, timed from the loss
}

void Acquisition::epoch(bool fix3d, uint32_t hAccMm, uint32_t nowMs) {
  anyEpoch_ = true;
  lastEpochMs_ = nowMs;
  if (fix_) {
    if (!fix3d) lose(nowMs);
    return;
  }
  if (!fix3d || hAccMm > t_.fixHAccMm) {
    holding_ = false;
    return;
  }
  if (!holding_) {
    holding_ = true;
    holdFromMs_ = nowMs;
  }
  if (nowMs - holdFromMs_ >= t_.fixHoldMs) {
    fix_ = true;
    ttffMs_ = nowMs - startMs_;
  }
}

void Acquisition::tick(uint32_t nowMs) {
  if (!anyEpoch_ || nowMs - lastEpochMs_ < t_.staleMs) return;
  anyEpoch_ = false;  // once per silence
  if (fix_) {
    lose(nowMs);
  } else {
    holding_ = false;
  }
}

AcqState Acquisition::state(uint32_t nowMs) const {
  if (fix_) return AcqState::Fix;
  return nowMs - startMs_ >= t_.timeoutMs ? AcqState::Timeout : AcqState::Acquiring;
}

}  // namespace ui
