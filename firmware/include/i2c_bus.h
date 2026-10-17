// The shared I²C bus — PLAN-ESP32.md §2.4.
//
// The display and the IMU sit on the same bus and are driven from the ui and imu tasks, on
// different cores, so every transaction holds I2cLock (rule 3 of §2.4). Hold it for one transaction
// or one display update, never across a wait.
#pragma once

void i2cBusBegin();  // Wire on PIN_I2C_SDA / PIN_I2C_SCL; once, from setup(), before any user

class I2cLock {
 public:
  I2cLock();
  ~I2cLock();
  I2cLock(const I2cLock&) = delete;
  I2cLock& operator=(const I2cLock&) = delete;
};
