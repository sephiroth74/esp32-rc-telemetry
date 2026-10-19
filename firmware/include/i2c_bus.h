// The shared I²C bus — PLAN-ESP32.md §2.4, POST check 0 (§3.3).
//
// The display and the IMU sit on the same bus and are driven from the ui and imu tasks, on
// different cores, so every transaction holds I2cLock (rule 3 of §2.4). Hold it for one transaction
// or one display update, never across a wait.
//
// Before the bus starts, it is recovered if it needs to be: a reset in the middle of a transaction
// can leave a device driving SDA low, waiting for clocks that never come, and nothing else on the
// bus would answer until it is freed.
#pragma once

enum class I2cBusState : unsigned char {
  Free,   // SDA was high
  Freed,  // SDA was held low, and the clocks of the recovery released it
  Stuck,  // SDA is still low: nothing on the bus can work
};

// Recovers the bus if needed, then starts Wire on PIN_I2C_SDA / PIN_I2C_SCL; once, from setup(),
// before any user.
void i2cBusBegin();
I2cBusState i2cBusState();  // as i2cBusBegin() found it, for POST check 0

class I2cLock {
 public:
  I2cLock();
  ~I2cLock();
  I2cLock(const I2cLock&) = delete;
  I2cLock& operator=(const I2cLock&) = delete;
};
