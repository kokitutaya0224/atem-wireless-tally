/*
 * LoRa タリーパケット定義（ベース局・受信機 共通）
 * 詳細: docs/lora-tally.md
 */
#pragma once
#include <Arduino.h>

#define LORA_MAGIC        0xA7
#define LORA_PACKET_LEN   10
#define LORA_FLAG_ATEM_OK 0x01

// パケット: [magic][flags][seq][pgm0][pgm1][pgm2][pvw0][pvw1][pvw2][xor]
// pgm/pvw はリトルエンディアン 24bit ビットマップ（bit0=カメラ1 ... bit19=カメラ20）

inline uint8_t loraChecksum(const uint8_t *buf) {
  uint8_t x = 0;
  for (int i = 0; i < LORA_PACKET_LEN - 1; i++) x ^= buf[i];
  return x;
}

inline void loraBuildPacket(uint8_t *buf, uint8_t flags, uint8_t seq,
                            uint32_t pgm, uint32_t pvw) {
  buf[0] = LORA_MAGIC;
  buf[1] = flags;
  buf[2] = seq;
  buf[3] = pgm & 0xFF;
  buf[4] = (pgm >> 8) & 0xFF;
  buf[5] = (pgm >> 16) & 0xFF;
  buf[6] = pvw & 0xFF;
  buf[7] = (pvw >> 8) & 0xFF;
  buf[8] = (pvw >> 16) & 0xFF;
  buf[9] = loraChecksum(buf);
}
