// Pin assignments — the single place they live. See PLAN-ESP32.md §2.1.
//
// Board: Seeed Studio XIAO ESP32-S3 Sense. Pad → GPIO mapping per PLAN.md:
// D0–D10 are GPIO 1, 2, 3, 4, 5, 6, 43, 44, 7, 8, 9. Ten of the 11 pads are
// allocated; D1 is the one spare.
#pragma once

#include <stdint.h>

// --- GNSS, UART1 (§5.2) -----------------------------------------------------
// GPIO43/44 are also U0TXD/U0RXD: the ROM bootloader prints its banner on
// GPIO43 at 115200 after every reset and there is no way to suppress it, so the
// GNSS configuration is re-applied on every boot (§2.2).
constexpr uint8_t PIN_GNSS_TX = 43;  // D6 → GM10 RX
constexpr uint8_t PIN_GNSS_RX = 44;  // D7 ← GM10 TX

// --- microSD over SDMMC, 1-bit (§2.3) ---------------------------------------
// Verified on the bench at 40 MHz with no external pull-ups (docs/hardware.md).
constexpr uint8_t PIN_SD_CLK = 7;  // D8, the SPI SCK pad
constexpr uint8_t PIN_SD_D0 = 8;   // D9, the SPI MISO pad
constexpr uint8_t PIN_SD_CMD = 9;  // D10, the SPI MOSI pad

// DAT3 is not on a pad — it arrives over the board-to-board connector — and is
// shared with the onboard user LED (active low). It must be driven HIGH before
// SD_MMC.begin() or the card comes up in SPI mode instead of SD mode. That is
// also why the onboard LED cannot serve as a status indicator; the display does.
//
// GPIO21, as Seeed's examples have it; their wiki's GPIO3 is wrong. Settled on
// the bench (§2.2, tools/sd-dat3-test): held low at the first CMD0 after
// power-up, GPIO21 sends the card to SPI mode and the mount fails; GPIO3 does
// not.
constexpr uint8_t PIN_SD_D3 = 21;

// --- I²C: display, IMU, GNSS magnetometer if present, future ADS1115 --------
// (§2.4, §2.6, §2.8). The pull-ups come from the modules on the bus.
constexpr uint8_t PIN_I2C_SDA = 5;  // D4
constexpr uint8_t PIN_I2C_SCL = 6;  // D5

// --- Local UI (§3) ----------------------------------------------------------
// The JS5208 five-way switch as a resistor ladder on one ADC pin (ADC1_CH0):
// common to GND, 10 kΩ pull-up to 3V3, OK straight to the pin, UP/DOWN/LEFT/RIGHT
// through 2.2k/4.7k/10k/22k. OK pulls the pin to 0 V, and the pin is RTC-capable,
// so OK wakes the chip from deep sleep (§3.1, §4.5). Windows in config.h.
// Until the switch is fitted: two buttons on the same ladder, OK straight to the
// pin and NAV through 10k — the LEFT level (KEY_LAYOUT_DEFAULT).
constexpr uint8_t PIN_KEYS = 1;  // D0

// --- RC inputs, through 10 kΩ with a BAT43 clamp to 3V3 and 100 kΩ down (§2.5) -
// Both RTC-capable: an RC pulse wakes the chip from deep sleep (ext1, §4.5).
constexpr uint8_t PIN_RC_THROTTLE = 3;  // D2
constexpr uint8_t PIN_RC_STEERING = 4;  // D3

// D1 (GPIO2) is spare, kept for the aux-channel remote trigger (§8, deferred
// and optional).
