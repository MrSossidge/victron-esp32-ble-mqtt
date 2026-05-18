#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct MpptData {
  float    batteryVoltage;  // V  (0.01V resolution)
  float    batteryCurrent;  // A  (0.1A resolution, positive = charging)
  float    pvPower;         // W  (1W resolution)
  float    yieldToday;      // Wh (10Wh resolution)
  float    pvVoltage;       // V  (not in MPPT advert, always 0)
  float    externalLoad; 
  uint8_t  chargeState;     // 0=off 3=bulk 4=absorption 5=float etc
  uint8_t  errorCode;       // 0 = no error
  bool     valid;           // false if decryption/parse failed
};

const char* chargeStateName(uint8_t state);

MpptData parseVictronMppt(const uint8_t* encKey, const uint8_t* data, size_t len);