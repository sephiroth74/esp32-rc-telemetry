#include "gnss_link.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "link_probe.h"
#include "pins.h"
#include "ubx.h"

namespace {

HardwareSerial port(1);  // UART1, on the pins of pins.h
bool started = false;
uint32_t found = 0;

// One rate: switch to it, drop what came at the last one, poll, and read until a frame or the
// timeout.
bool tryBaud(uint32_t baud) {
  if (!started) {
    port.setRxBufferSize(GNSS_UART_RX_BUFFER);  // before begin(), which allocates it
    port.begin(baud, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
    started = true;
  } else {
    port.updateBaudRate(baud);
  }
  while (port.available()) port.read();

  uint8_t poll[ubx::kHeaderBytes + ubx::kChecksumBytes];
  port.write(poll, ubx::encode(0x0A, 0x04, nullptr, 0, poll, sizeof poll));  // MON-VER

  ubx::LinkProbe probe;
  uint8_t buf[256];
  uint32_t start = millis();
  while (millis() - start < GNSS_BAUD_SCAN_TIMEOUT_MS) {
    size_t n = port.available();
    if (!n) {
      delay(5);
      continue;
    }
    n = port.readBytes(buf, n < sizeof buf ? n : sizeof buf);
    probe.push(buf, n);
    if (probe.found()) return true;
  }
  return false;
}

}  // namespace

uint32_t gnssLinkScan() {
  Preferences nvs;
  nvs.begin("gnss", /*readOnly=*/false);
  uint32_t last = nvs.getUInt("baud", 0);
  found = 0;
  if (last && tryBaud(last)) found = last;
  for (uint32_t baud : GNSS_BAUD_SCAN) {
    if (found) break;
    if (baud != last && tryBaud(baud)) found = baud;
  }
  if (found && found != last) nvs.putUInt("baud", found);
  nvs.end();
  return found;
}

uint32_t gnssLinkBaud() { return found; }

HardwareSerial& gnssSerial() { return port; }
