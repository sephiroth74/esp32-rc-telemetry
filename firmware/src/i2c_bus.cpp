#include "i2c_bus.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace {

SemaphoreHandle_t busMutex = nullptr;

}  // namespace

void i2cBusBegin() {
  busMutex = xSemaphoreCreateMutex();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
}

I2cLock::I2cLock() { xSemaphoreTake(busMutex, portMAX_DELAY); }

I2cLock::~I2cLock() { xSemaphoreGive(busMutex); }
