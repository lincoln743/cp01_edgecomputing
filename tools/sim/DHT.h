/* Shim da biblioteca Adafruit DHT para o harness de validacao no PC. */
#pragma once
#include <Arduino.h>
#define DHT22 22
class DHT {
 public:
  DHT(int, int) {}
  void  begin() {}
  float readTemperature() { return sim_dht_ok ? sim_temp : NAN; }
  float readHumidity()    { return sim_dht_ok ? sim_hum  : NAN; }
};
