// Compile-time constants and feature flags. Every value here traces back to a
// decision in PLAN-ESP32.md; the section is cited so the two cannot drift apart
// silently. Anything a user may want to change at runtime belongs in
// /config.json on LittleFS instead (settings.h), not here.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FW_VERSION "0.1.0-dev"

// --- GNSS (§5.1, §5.2) ------------------------------------------------------
constexpr uint32_t GNSS_BAUD = 230400;             // CFG-UART1-BAUDRATE
constexpr uint32_t GNSS_BAUD_SCAN[] = {230400, 115200, 38400, 9600};
constexpr uint32_t GNSS_BAUD_SCAN_TIMEOUT_MS = 1500;  // per attempt
constexpr uint16_t GNSS_NAV_PERIOD_MS = 100;       // CFG-RATE-MEAS → 10 Hz, the M10 maximum
constexpr uint8_t GNSS_SAT_RATE_DIVISOR = 10;      // NAV-SAT at 1 Hz, logged not just debugged
constexpr uint8_t GNSS_DYNMODEL = 4;               // automotive; A/B against 8 on track
constexpr size_t GNSS_UART_RX_BUFFER = 2048;
constexpr uint8_t GNSS_FRAME_QUEUE_DEPTH = 32;     // >3 s of headroom at 10 Hz (§4.2)

// --- Logging and the SD write path (§4.4) -----------------------------------
constexpr size_t LOG_RING_BYTES = 256 * 1024;          // PSRAM, ~75 s of stall absorption
constexpr size_t LOG_WRITE_CHUNK = 32 * 1024;
constexpr uint32_t LOG_FLUSH_INTERVAL_MS = 5000;       // bounds loss on an ungraceful stop
constexpr size_t LOG_EXTENT_BYTES = 4u * 1024 * 1024;  // ~20 min per extent
constexpr size_t LOG_EXTENT_REFILL_BYTES = 1u * 1024 * 1024;  // extend when this much is left
constexpr uint32_t LOG_SLOW_WRITE_MS = 250;            // counted per session and reported

// --- Power-on self-test (§3.3) ----------------------------------------------
constexpr size_t POST_PROBE_BYTES = 256 * 1024;   // one probe answers "writable?" and "how fast?"
constexpr uint32_t POST_MIN_FREE_MB = 64;         // sd.min_free_mb — below this, logging refused
constexpr uint32_t POST_WARN_FREE_MB = 256;       // sd.warn_free_mb — below this, warning only
constexpr uint32_t POST_SLOW_CARD_KBPS = 200;     // below this the card is called out
constexpr uint32_t POST_RC_EDGE_WINDOW_MS = 500;  // no edges → no power-loss detection (§4.5)

// --- Button gestures (§3.1) -------------------------------------------------
constexpr uint16_t BTN_DEBOUNCE_MS = 25;
constexpr uint16_t BTN_SHORT_MAX_MS = 800;   // 800–1500 ms is deliberately ambiguous → ignored
constexpr uint16_t BTN_LONG_MS = 1500;       // fires while still held, not on release

// --- LED patterns (§3.2) ----------------------------------------------------
constexpr uint16_t LED_SLOW_BLINK_MS = 500;  // 1 Hz, 50 % duty
constexpr uint16_t LED_FAST_BLINK_MS = 125;  // 4 Hz, 50 % duty
constexpr uint16_t LED_FLASH_MS = 80;
constexpr uint16_t LED_CODE_ON_MS = 200;     // blink code: N × on/gap, then pause
constexpr uint16_t LED_CODE_GAP_MS = 200;
constexpr uint16_t LED_CODE_PAUSE_MS = 1200;
constexpr uint8_t LED_BRIGHTNESS = 77;       // ~30 % — LDO budget, and readable at night

// --- RC capture (§4.3, §4.5) ------------------------------------------------
constexpr uint16_t RC_PULSE_MIN_US = 800;    // widths outside this are rejected and counted:
constexpr uint16_t RC_PULSE_MAX_US = 2200;   // the ESC may burst Smart telemetry on CH2 (§2.8)
constexpr uint16_t RC_SIGNAL_LOST_MS = 200;  // no edges for this long → the car was switched off

// --- IMU (§2.6) -------------------------------------------------------------
constexpr uint16_t IMU_SAMPLE_HZ = 200;
constexpr uint8_t IMU_I2C_ADDR = 0x6A;   // LSM6DSO32, 0x6B with SDO high
constexpr bool IMU_LOG_RAW = false;      // the 100 Hz raw stream — open question 7

// --- Task placement (§4.1) --------------------------------------------------
// Core 1 is the acquisition core: it is where loop() already runs
// (ARDUINO_RUNNING_CORE=1) and it stays clear of the radio. Core 0 hosts the
// NimBLE host task, so the BLE task belongs there and the logger goes with it to
// keep blocking SD calls off the timing-critical core.
constexpr int TASK_GNSS_CORE = 1, TASK_GNSS_PRIO = 5;
constexpr int TASK_IMU_CORE = 1, TASK_IMU_PRIO = 4;
constexpr int TASK_LOGGER_CORE = 0, TASK_LOGGER_PRIO = 3;
constexpr int TASK_UI_CORE = 0, TASK_UI_PRIO = 2;
constexpr int TASK_BLE_CORE = 0, TASK_BLE_PRIO = 2;

// --- Track resolution (§7.1) ------------------------------------------------
constexpr float TRACK_MATCH_RADIUS_M = 300.0f;  // track.match_radius_m default
