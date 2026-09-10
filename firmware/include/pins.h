// Pin assignments — the single place they live. See PLAN-ESP32.md §2.1.
//
// Board: Seeed Studio XIAO ESP32-S3 Sense. Pad → GPIO mapping per PLAN.md:
// D0–D10 are GPIO 1, 2, 3, 4, 5, 6, 43, 44, 7, 8, 9. All 11 pads are allocated.
#pragma once

#include <stdint.h>

// --- GNSS, UART1 (§5.2) -----------------------------------------------------
// GPIO43/44 are also U0TXD/U0RXD: the ROM bootloader prints its banner on
// GPIO43 at 115200 after every reset and there is no way to suppress it, so the
// GNSS configuration is re-applied on every boot (§2.2).
constexpr uint8_t PIN_GNSS_TX = 43;  // D6 → GM10 RX
constexpr uint8_t PIN_GNSS_RX = 44;  // D7 ← GM10 TX

// --- microSD over SDMMC, 1-bit (§2.3) ---------------------------------------
constexpr uint8_t PIN_SD_CLK = 7;  // D8, the SPI SCK pad
constexpr uint8_t PIN_SD_D0 = 8;   // D9, the SPI MISO pad
constexpr uint8_t PIN_SD_CMD = 9;  // D10, the SPI MOSI pad

// DAT3 is not on a pad — it arrives over the board-to-board connector — and is
// shared with the onboard user LED (active low). It must be driven HIGH before
// SD_MMC.begin() or the card comes up in SPI mode instead of SD mode. That is
// also why the onboard LED cannot serve as a status indicator, and why there is
// an external WS2812B.
//
// UNRESOLVED (§2.2): Seeed's examples say GPIO21, their wiki says GPIO3. If it
// turns out to be GPIO3, PIN_RC_THROTTLE below has to move and the button goes
// onto the I²C bus. Settle this on hardware before freezing the map.
constexpr uint8_t PIN_SD_D3 = 21;

// --- I²C: IMU, GNSS magnetometer if present, future ADS1115 (§2.6, §2.8) ----
constexpr uint8_t PIN_I2C_SDA = 5;  // D4
constexpr uint8_t PIN_I2C_SCL = 6;  // D5

// --- Local UI (§3) ----------------------------------------------------------
constexpr uint8_t PIN_BUTTON = 1;    // D0, to GND, INPUT_PULLUP. RTC-capable, so
                                     // it can wake the chip from deep sleep (§4.5).
constexpr uint8_t PIN_LED_DATA = 2;  // D1, WS2812B data — one pin instead of three

// --- RC inputs, level-shifted through a 74LVC2G17 (§2.5) --------------------
constexpr uint8_t PIN_RC_THROTTLE = 3;  // D2, CH2. At risk, see PIN_SD_D3 above.
constexpr uint8_t PIN_RC_STEERING = 4;  // D3, CH1
