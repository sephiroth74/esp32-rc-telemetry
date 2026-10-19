#include "i2c_bus.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace {

SemaphoreHandle_t busMutex = nullptr;
I2cBusState state = I2cBusState::Free;

constexpr uint32_t kHalfClockUs = 5;  // 100 kHz, slow enough for any device on the bus

// Up to nine SCL clocks, so that a device mid-byte can finish it and let go of SDA, then a STOP —
// SDA rising while SCL is high — to end the transaction it thought was going on. Both lines are
// driven open-drain, so the bus pull-ups do the rising, as they do for Wire.
I2cBusState recover() {
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delayMicroseconds(kHalfClockUs);
  if (digitalRead(PIN_I2C_SDA)) return I2cBusState::Free;

  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9 && !digitalRead(PIN_I2C_SDA); ++i) {
    digitalWrite(PIN_I2C_SCL, LOW);
    delayMicroseconds(kHalfClockUs);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(kHalfClockUs);
  }
  pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SDA, LOW);
  delayMicroseconds(kHalfClockUs);
  digitalWrite(PIN_I2C_SCL, HIGH);
  delayMicroseconds(kHalfClockUs);
  digitalWrite(PIN_I2C_SDA, HIGH);
  delayMicroseconds(kHalfClockUs);

  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delayMicroseconds(kHalfClockUs);
  return digitalRead(PIN_I2C_SDA) ? I2cBusState::Freed : I2cBusState::Stuck;
}

}  // namespace

void i2cBusBegin() {
  busMutex = xSemaphoreCreateMutex();
  state = recover();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
}

I2cBusState i2cBusState() { return state; }

I2cLock::I2cLock() { xSemaphoreTake(busMutex, portMAX_DELAY); }

I2cLock::~I2cLock() { xSemaphoreGive(busMutex); }
