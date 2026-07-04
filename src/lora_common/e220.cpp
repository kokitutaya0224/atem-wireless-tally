#include "e220.h"

// モード切替後の安定待ち。AUX 監視の代わりに余裕を持ったディレイを使う
static const uint16_t MODE_SWITCH_DELAY_MS = 150;

void E220::begin(Stream &serial, uint8_t m0Pin, uint8_t m1Pin) {
  _serial = &serial;
  _m0 = m0Pin;
  _m1 = m1Pin;
  pinMode(_m0, OUTPUT);
  pinMode(_m1, OUTPUT);
}

void E220::setMode(uint8_t m0, uint8_t m1) {
  digitalWrite(_m0, m0);
  digitalWrite(_m1, m1);
  delay(MODE_SWITCH_DELAY_MS);
}

bool E220::configure(uint16_t addr, uint8_t reg0, uint8_t reg1,
                     uint8_t channel, uint8_t reg3) {
  setMode(HIGH, HIGH);  // mode 3: コンフィグ（UART 9600固定）

  while (_serial->available()) _serial->read();

  const uint8_t values[6] = {
    (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
    reg0, reg1, channel, reg3
  };
  uint8_t cmd[9] = { 0xC0, 0x00, 0x06 };
  memcpy(cmd + 3, values, 6);
  _serial->write(cmd, sizeof(cmd));
  _serial->flush();

  // 応答: 0xC1 0x00 0x06 + 設定値エコー（計9バイト）
  uint8_t resp[9];
  size_t got = 0;
  unsigned long start = millis();
  while (got < sizeof(resp) && millis() - start < 1000) {
    int c = _serial->read();
    if (c >= 0) resp[got++] = (uint8_t)c;
    else delay(1);
  }

  bool ok = (got == sizeof(resp)) &&
            resp[0] == 0xC1 && resp[1] == 0x00 && resp[2] == 0x06 &&
            memcmp(resp + 3, values, 6) == 0;

  setMode(LOW, LOW);  // mode 0: 通常送受信
  return ok;
}

void E220::send(const uint8_t *data, size_t len) {
  _serial->write(data, len);
}

int E220::read() {
  return _serial->read();
}

int E220::available() {
  return _serial->available();
}
