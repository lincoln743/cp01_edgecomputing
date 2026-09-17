/* Shim de Arduino para rodar o MESMO firmware no PC (harness de validacao).
   Nao faz parte do build do ESP32 - PlatformIO compila apenas src/.        */
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <math.h>

#define HIGH 1
#define LOW  0
#define OUTPUT 1
#define INPUT  0
#define ADC_11db 3
#define F(x) (x)

// ---- estado do simulador (controlado por sim_main.cpp) ----
inline uint32_t sim_millis   = 0;
inline float    sim_temp     = 5.0f;
inline float    sim_hum      = 60.0f;
inline bool     sim_dht_ok   = true;
inline int      sim_ldr      = 300;
inline int      sim_pin[64]  = {0};
inline unsigned sim_tone_hz  = 0;

inline uint32_t millis()                        { return sim_millis; }
inline void     delay(uint32_t)                 {}
inline void     pinMode(int, int)               {}
inline void     digitalWrite(int p, int v)      { if (p >= 0 && p < 64) sim_pin[p] = v; }
inline int      analogRead(int)                 { return sim_ldr; }
inline void     analogReadResolution(int)       {}
inline void     analogSetPinAttenuation(int,int){}
inline void     tone(int, unsigned f)           { sim_tone_hz = f; }
inline void     noTone(int)                     { sim_tone_hz = 0; }

struct FakeSerial {
  void begin(unsigned long) {}
  void println()                  { std::printf("\n"); }
  void println(const char* s)     { std::printf("%s\n", s); }
  void print(const char* s)       { std::printf("%s", s); }
  template <typename... A> void printf(const char* f, A... a) { std::printf(f, a...); }
};
inline FakeSerial Serial;
