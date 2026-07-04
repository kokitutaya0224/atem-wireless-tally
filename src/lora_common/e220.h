/*
 * E220-900T22S(JP) 最小ドライバ
 * - mode 3（M0=H, M1=H, UART 9600固定）でレジスタを書き込み、mode 0 で運用
 * - AUX ピンは未使用（固定ディレイで代替）
 */
#pragma once
#include <Arduino.h>
#include <Stream.h>

class E220 {
public:
  // serial は 9600bps で begin 済みであること
  void begin(Stream &serial, uint8_t m0Pin, uint8_t m1Pin);

  // レジスタ 0x00-0x05 を書き込み、応答を検証して通常モードへ移行する
  // 失敗時は false（リトライは呼び出し側）
  bool configure(uint16_t addr, uint8_t reg0, uint8_t reg1,
                 uint8_t channel, uint8_t reg3);

  void send(const uint8_t *data, size_t len);
  int  read();       // -1 = データなし
  int  available();

private:
  void setMode(uint8_t m0, uint8_t m1);
  Stream *_serial = nullptr;
  uint8_t _m0 = 255, _m1 = 255;
};
