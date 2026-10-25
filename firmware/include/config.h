// Compile-time constants and feature flags. Every value here traces back to a
// decision in PLAN-ESP32.md; the section is cited so the two cannot drift apart
// silently. Anything a user may want to change at runtime belongs in
// /config.json on LittleFS instead (settings.h), not here.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FW_VERSION "0.1.0-dev"

// --- GNSS (§5.1, §5.2) ------------------------------------------------------
constexpr uint32_t GNSS_BAUD = 230400;  // CFG-UART1-BAUDRATE
constexpr uint32_t GNSS_BAUD_SCAN[] = {230400, 115200, 38400, 9600};
constexpr uint32_t GNSS_BAUD_SCAN_TIMEOUT_MS = 1500;  // per attempt
constexpr uint16_t GNSS_NAV_PERIOD_MS = 100;          // CFG-RATE-MEAS → 10 Hz, the M10 maximum
constexpr uint8_t GNSS_SAT_RATE_DIVISOR = 10;         // NAV-SAT at 1 Hz, logged not just debugged
constexpr uint8_t GNSS_DYNMODEL = 4;                  // automotive; A/B against 8 on track
constexpr uint32_t GNSS_CFG_ANSWER_MS = 1000;         // VALGET's response, VALSET's ACK (check 9)
constexpr uint32_t GNSS_CFG_RETRY_MS = 10000;         // check 9 unanswered: tried again this often
constexpr size_t GNSS_UART_RX_BUFFER = 2048;
constexpr uint32_t GNSS_READ_TIMEOUT_MS = 20;   // the gnss task's read: a short block (§4.2)
constexpr uint32_t GNSS_BENCH_LOG_MS = 10000;   // the rate, fix and losses on the console
// What the GNSS screen calls a fix (§3.4): 3D, hAcc within 5 m, held 3 s; Timeout after 90 s.
constexpr uint32_t GNSS_FIX_HACC_MM = 5000;
constexpr uint32_t GNSS_FIX_HOLD_MS = 3000;
constexpr uint32_t GNSS_ACQ_TIMEOUT_MS = 90000;
constexpr uint32_t GNSS_STALE_MS = 2000;  // no epoch this long: no fix, and the link has gone quiet

// --- Logging and the SD write path (§4.4) -----------------------------------
constexpr size_t LOG_RING_BYTES = 256 * 1024;  // PSRAM, ~75 s of stall absorption
constexpr size_t LOG_WRITE_CHUNK = 32 * 1024;
constexpr uint32_t LOG_FLUSH_INTERVAL_MS = 5000;              // bounds loss on an ungraceful stop
constexpr size_t LOG_EXTENT_BYTES = 4u * 1024 * 1024;         // ~20 min per extent
constexpr size_t LOG_EXTENT_REFILL_BYTES = 1u * 1024 * 1024;  // extend when this much is left
constexpr uint32_t LOG_SLOW_WRITE_MS = 250;                   // counted per session and reported
constexpr uint32_t LOG_CLOSE_WAIT_MS = 3000;  // a stop waits this long for the card to be done
// A session left open, closed at the next boot (§4.5): read in pieces of this size, in PSRAM, and
// searched this far past its last record.
constexpr size_t LOG_RECOVER_PIECE_BYTES = 128 * 1024;
constexpr size_t LOG_RECOVER_GAP_BYTES = 64 * 1024;

// --- Power-on self-test (§3.3) ----------------------------------------------
constexpr size_t POST_PROBE_BYTES = 256 * 1024;   // one probe answers "writable?" and "how fast?"
constexpr uint32_t POST_MIN_FREE_MB = 64;         // sd.min_free_mb — below this, logging refused
constexpr uint32_t POST_WARN_FREE_MB = 256;       // sd.warn_free_mb — below this, warning only
constexpr uint32_t POST_SLOW_CARD_KBPS = 200;     // below this the card is called out
constexpr uint32_t POST_RC_EDGE_WINDOW_MS = 500;  // edges within this: the inputs present (§4.3)

// --- Keys: the five-way switch on one ADC pin (§3.1) ------------------------
// Ladder levels against a 10 kΩ pull-up, as the ADC reads them. Measured on the bench
// (docs/hardware.md): OK 0 mV, LEFT/NAV 1623 mV, none 3120 mV — the ADC saturates there,
// not at 3.3 V. Still nominal until the JS5208 is fitted: UP 600, DOWN 1050, RIGHT 2270.
// Each bound sits halfway between neighbouring levels.
constexpr uint16_t KEY_MV_BOUNDS[] = {300, 825, 1335, 1945, 2695};  // OK|UP|DOWN|LEFT|RIGHT|none
constexpr uint16_t KEY_SAMPLE_HZ = 100;
constexpr uint8_t KEY_CONFIRM_SAMPLES = 2;  // same window twice in a row, or it is a bounce
// OK released after KEY_SHORT_MAX_MS but before the long press does nothing.
constexpr uint16_t KEY_SHORT_MAX_MS = 800;
constexpr uint16_t KEY_LONG_MS = 1500;  // fires while still held, not on release
constexpr uint16_t KEY_HOLD_BAR_FROM_MS = 300;
constexpr uint16_t KEY_REPEAT_DELAY_MS = 500;  // UP/DOWN held
constexpr uint16_t KEY_REPEAT_MS = 200;

// Which hardware drives the ladder. Two buttons until the JS5208 is fitted: OK at
// the OK level, NAV at the LEFT level (10 kΩ), NAV short = DOWN, NAV held = LEFT.
// keys.layout in config.json overrides the default, so the swap needs no reflash.
enum class KeyLayout : uint8_t { TwoButtons, FiveWay };
constexpr KeyLayout KEY_LAYOUT_DEFAULT = KeyLayout::TwoButtons;
constexpr uint16_t KEY_NAV_BACK_MS = 1000;  // two-button layout: NAV held this long = back
constexpr uint8_t KEY_NAV_WINDOW = 3;       // NAV's window in KEY_MV_BOUNDS: the LEFT one

// --- Menus and dialogs (§3.1) -----------------------------------------------
constexpr uint32_t UI_MENU_TIMEOUT_MS = 30000;    // back to Home without a key
constexpr uint32_t UI_DIALOG_TIMEOUT_MS = 10000;  // a dialog left alone answers "no"

// --- Display (§2.4, §3.2) ---------------------------------------------------
constexpr uint8_t DISPLAY_I2C_ADDR = 0x3C;  // 7-bit; 0x78 in 8-bit listings
// 5 Hz at most, and dirty pages only: the IMU shares the bus.
constexpr uint16_t DISPLAY_REFRESH_MS = 200;
constexpr uint16_t DISPLAY_TOAST_MS = 1500;
constexpr uint16_t DISPLAY_ACK_INVERT_MS = 80;  // long press acknowledged by inverting the screen
constexpr uint16_t DISPLAY_POST_DEGRADED_MS = 3000;  // degraded checks stay listed this long
// The display settings of §3.8 as indices into lib/ui's label tables, and what they mean to the
// panel: the SSD1306 contrast, 0–255. Dimmed is the lowest, still readable, never blank (§3.2).
constexpr uint8_t DISPLAY_BRIGHTNESS_DEFAULT = 2;      // high
constexpr uint8_t DISPLAY_CONTRAST[] = {16, 96, 255};  // low, medium, high
constexpr uint8_t DISPLAY_CONTRAST_DIMMED = 0;
constexpr uint8_t DISPLAY_DIM_DELAY_DEFAULT = 1;  // 30 s
// Text size, a display setting too: 0 small (5x8, 21×8), 1 large (6x13B), picked on the bench.
constexpr uint8_t DISPLAY_TEXT_SIZE_DEFAULT = 1;

// --- Sessions, automatic start and stop (§3.6) ------------------------------
constexpr float SESSION_START_SPEED_KMH = 5.0f;  // with a 3D fix...
constexpr uint16_t SESSION_START_SPEED_MS = 2000;
// ...or the throttle this far off neutral, with a calibrated profile.
constexpr uint8_t SESSION_START_THROTTLE_PCT = 10;
constexpr uint16_t SESSION_START_THROTTLE_MS = 1000;
constexpr float SESSION_STILL_SPEED_KMH = 2.0f;    // standing still: below this speed...
constexpr uint8_t SESSION_STILL_THROTTLE_PCT = 5;  // ...and within this of neutral
constexpr uint8_t SESSION_AUTO_STOP_MIN = 5;       // session.auto_stop_min default
constexpr uint32_t SESSION_REARM_MS = 5000;  // after a stop by hand: still this long before a start

// --- Sleep and wake (§4.5) --------------------------------------------------
constexpr uint32_t SLEEP_NOTICE_MS = 10000;         // from IDLE, before SLEEP; any key cancels
constexpr uint32_t SLEEP_IDLE_MS = 5u * 60 * 1000;  // no RC signal, no USB host, no key
constexpr uint32_t SLEEP_SIM_WAKE_S = 20;  // the bench simulation's wake, for a press of OK

// --- Car profiles (§3.8) ----------------------------------------------------
constexpr uint8_t CAR_PROFILES_MAX = 8;
constexpr uint8_t CAR_PROFILE_NAME_MAX = 8;  // what the status bar has room for

// --- RC capture (§4.3, §4.5) ------------------------------------------------
// Defaults until a car is calibrated; after that the window comes from its calibration.
constexpr uint16_t RC_PULSE_MIN_US = 800;    // widths outside this are rejected and counted:
constexpr uint16_t RC_PULSE_MAX_US = 2200;   // some ESCs send telemetry back on the throttle wire
constexpr uint16_t RC_FRAME_MIN_US = 3000;   // fast digital modes, ~333 Hz
constexpr uint16_t RC_FRAME_MAX_US = 22000;  // analog-servo modes, ~45 Hz
constexpr uint16_t RC_SIGNAL_LOST_MS = 200;  // no valid pulse for this long: the signal stopped
constexpr uint32_t RC_SAMPLE_PERIOD_US = 20000;  // 50 Hz, the session header's RC period (protocol)
constexpr uint16_t RC_WINDOW_MARGIN_US = 100;  // a calibrated input takes its ends widened by this
constexpr uint16_t RC_STALE_MS = 100;  // no valid pulse for this long: the input logs 0, not its last

// --- IMU (§2.6) -------------------------------------------------------------
constexpr uint16_t IMU_SAMPLE_HZ = 200;
constexpr uint8_t IMU_I2C_ADDR = 0x6A;  // LSM6DSO32, 0x6B with SDO high
constexpr bool IMU_LOG_RAW = false;     // the 100 Hz raw stream — open question 7

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
constexpr float TRACK_LOOKUP_MOVE_M = 50.0f;  // in IDLE, the screens' track looked up again this far on
