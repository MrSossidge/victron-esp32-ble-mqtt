#include "victron_ble.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <mbedtls/aes.h>

// ---------------------------------------------------------------------------
// Victron advertisement manufacturer data layout (after 2-byte company ID):
//
//   Byte 0-1:  prefix              (uint16 LE)
//   Byte 2-3:  model_id            (uint16 LE)
//   Byte 4:    readout_type
//   Byte 5-6:  IV / nonce          (uint16 LE)
//   Byte 7:    key check byte      (must equal encKey[0])
//   Byte 8+:   AES-128-CTR encrypted payload
//
// AES-128-CTR: 128-bit little-endian counter, initial value = nonce (uint16)
// Matches Python: Counter.new(128, initial_value=iv, little_endian=True)
// ---------------------------------------------------------------------------

static bool aes128ctrDecrypt(const uint8_t key[16], uint16_t nonce,
                              const uint8_t* in, uint8_t* out, size_t len) {
  // 16-byte little-endian IV — nonce in bytes 0-1, rest zero
  uint8_t iv[16] = {0};
  iv[0] = nonce & 0xFF;
  iv[1] = (nonce >> 8) & 0xFF;

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (mbedtls_aes_setkey_enc(&ctx, key, 128) != 0) {
    mbedtls_aes_free(&ctx);
    return false;
  }
  size_t nc_off = 0;
  uint8_t stream_block[16] = {0};
  int ret = mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, iv, stream_block, in, out);
  mbedtls_aes_free(&ctx);
  return ret == 0;
}

// ---------------------------------------------------------------------------
// BitReader — mirrors Python BitReader exactly, reads fields LSB first
// ---------------------------------------------------------------------------

struct BitReader {
  const uint8_t* data;
  size_t len;
  size_t bitPos;

  BitReader(const uint8_t* d, size_t l) : data(d), len(l), bitPos(0) {}

  uint32_t readUnsigned(int bits) {
    uint32_t result = 0;
    for (int i = 0; i < bits; i++) {
      size_t byteIdx = bitPos / 8;
      size_t bitIdx  = bitPos % 8;
      if (byteIdx < len) {
        result |= (uint32_t)((data[byteIdx] >> bitIdx) & 1) << i;
      }
      bitPos++;
    }
    return result;
  }

  int32_t readSigned(int bits) {
    uint32_t raw = readUnsigned(bits);
    if (raw & (1u << (bits - 1))) {
      raw |= ~((1u << bits) - 1);
    }
    return (int32_t)raw;
  }
};

// ---------------------------------------------------------------------------
// SmartSolar MPPT decrypted payload (matches Python solar_charger.py exactly)
//
//   8  bits: charge_state
//   8  bits: charger_error
//   16 bits: battery_voltage  (signed, /100 → V)
//   16 bits: battery_current  (signed, /10  → A)
//   16 bits: yield_today      (unsigned, x10 → Wh)
//   16 bits: solar_power      (unsigned, 1W/bit)
//   9  bits: external_device_load (unsigned, /10 → A, not published)
// ---------------------------------------------------------------------------

MpptData parseVictronMppt(const uint8_t* encKey, const uint8_t* data, size_t len) {
  MpptData result = {};
  result.valid = false;

  // Need at least 9 bytes: 5 header + 2 nonce + 1 key check + 1 encrypted
  if (len < 9) return result;

  uint16_t nonce         = (uint16_t)data[5] | ((uint16_t)data[6] << 8);
  uint8_t  keyCheck      = data[7];
  const uint8_t* encrypted = data + 8;
  size_t encLen          = len - 8;

  // Key check: first byte must equal encKey[0]
  if (keyCheck != encKey[0]) {
    Serial.printf("[BLE] Key check failed: got 0x%02X expected 0x%02X\n",
                  keyCheck, encKey[0]);
    return result;
  }

  uint8_t decrypted[32] = {0};
  if (!aes128ctrDecrypt(encKey, nonce, encrypted, decrypted, encLen)) {
    Serial.println("[BLE] AES decryption failed");
    return result;
  }

  BitReader reader(decrypted, encLen);

  uint32_t chargeState  = reader.readUnsigned(8);
  uint32_t chargerError = reader.readUnsigned(8);
  int32_t  batV_raw     = reader.readSigned(16);
  int32_t  batI_raw     = reader.readSigned(16);
  uint32_t yieldToday   = reader.readUnsigned(16);
  uint32_t solarPower   = reader.readUnsigned(16);
  uint32_t extLoad = reader.readUnsigned(9);  // 9 bits, 0.1A/bit
  result.externalLoad = (extLoad != 0x1FF) ? extLoad / 10.0f : 0.0f;

  result.chargeState    = (uint8_t)chargeState;
  result.errorCode      = (uint8_t)chargerError;
  result.batteryVoltage = (batV_raw   != 0x7FFF) ? batV_raw  / 100.0f : 0.0f;
  result.batteryCurrent = (batI_raw   != 0x7FFF) ? batI_raw  / 10.0f  : 0.0f;
  result.yieldToday     = (yieldToday != 0xFFFF) ? yieldToday * 10.0f : 0.0f;
  result.pvPower        = (solarPower != 0xFFFF) ? (float)solarPower   : 0.0f;
  result.pvVoltage      = 0.0f;
  result.valid          = true;

  return result;
}

const char* chargeStateName(uint8_t state) {
  switch (state) {
    case 0:   return "off";
    case 2:   return "fault";
    case 3:   return "bulk";
    case 4:   return "absorption";
    case 5:   return "float";
    case 6:   return "storage";
    case 7:   return "equalize";
    case 245: return "starting";
    case 246: return "repeated_absorption";
    case 247: return "recondition";
    case 248: return "battery_safe";
    case 249: return "active";
    case 252: return "external_control";
    case 255: return "not_available";
    default:  return "unknown";
  }
}