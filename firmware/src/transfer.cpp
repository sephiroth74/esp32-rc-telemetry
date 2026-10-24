#include "transfer.h"

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>

#include "card.h"

namespace {

constexpr uint32_t kSector = 512;

// Constructed before USB starts, so that the disk is part of the device from the first
// enumeration; without a medium until TRANSFER (USBMSC.cpp: media_present starts false).
USBMSC msc;
volatile bool ejected = false;

// TinyUSB hands over whole sectors at the LBA it has reached, the offset within one being 0 for
// a buffer of whole sectors (CONFIG_TINYUSB_MSC_BUFSIZE, 4096). Anything else is refused.
int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  if (offset || bufsize % kSector) return -1;
  return cardRawRead(lba, buffer, bufsize / kSector) ? int32_t(bufsize) : -1;
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (offset || bufsize % kSector) return -1;
  return cardRawWrite(lba, buffer, bufsize / kSector) ? int32_t(bufsize) : -1;
}

// macOS's Eject is a START STOP UNIT with load_eject set and start clear. The medium goes with it,
// so that the host does not mount the disk again behind the user's back.
bool onStartStop(uint8_t, bool start, bool loadEject) {
  if (loadEject && !start) {
    ejected = true;
    msc.mediaPresent(false);
  }
  return true;
}

}  // namespace

bool transferEnter() {
  uint32_t sectors = 0;
  if (!cardRawBegin(sectors)) {
    cardRawEnd();
    cardMount();  // as it was: the logger and the screens find the card where they left it
    return false;
  }
  ejected = false;
  msc.vendorID("RCT");
  msc.productID("Telemetry card");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.isWritable(true);
  msc.begin(sectors, kSector);
  msc.mediaPresent(true);
  Serial.printf("transfer: %lu sectors on USB\n", (unsigned long)sectors);
  return true;
}

bool transferLeave() {
  msc.mediaPresent(false);
  msc.end();
  cardRawEnd();
  bool ok = cardMount() == CardMount::Ok;
  Serial.printf("transfer: left, card %s\n", ok ? "mounted again" : "would not mount");
  return ok;
}

bool transferHostAttached() { return bool(USB); }

bool transferEjected() { return ejected; }
