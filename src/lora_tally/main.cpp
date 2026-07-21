/*
 * ATEM LoRa タリー受信機
 * ベース局が E220-900T22S(JP) でブロードキャストするタリーパケットを受信し、
 * 自分のカメラ番号の状態を RGB LED に表示する。WiFi 接続は不要。
 * 配線・プロトコル: docs/lora-tally.md
 *
 * LED表示:
 *   赤点灯       = PROGRAM (ON AIR)
 *   緑点灯       = PREVIEW
 *   消灯         = 非選択
 *   青点滅       = ベース局は生きているが ATEM 未接続
 *   マゼンタ点滅 = LoRa 電波ロスト（ベース局停止 or 圏外）
 *
 * カメラ番号設定: AP "ATEM-Tally-RX" に接続して http://192.168.4.1
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include "../lora_common/e220.h"
#include "../lora_common/lora_protocol.h"

// ============================================================
// ハードウェア設定
// ============================================================

#define PIN_R       D5
#define PIN_G       D6
#define PIN_B       D7
#define RESET_PIN   D0   // GPIO16: ボタンは 3V3 へ接続（押下=HIGH）
#define PWM_RANGE   255

// E220-900T22S(JP)
#define LORA_M0     D3   // GPIO0
#define LORA_M1     D4   // GPIO2
#define LORA_TX     D1   // GPIO5  -> E220 RXD
#define LORA_RX     D2   // GPIO4  <- E220 TXD

// 無線設定（ベース局と一致させること）
#define LORA_ADDR   0x0000
#define LORA_REG0   0x68   // UART 9600 + AirRate SF7/BW125 (5,469bps)
#define LORA_REG1   0x01   // ペイロード200B, 送信出力13dBm
#define LORA_CH     0x0A   // CH10 = 922.6MHz
#define LORA_REG3   0x80   // RSSIバイト付加ON, 透過送信

#define LORA_TIMEOUT_MS 2500  // これ以上パケットが来なければ電波ロスト表示

const char* AP_SSID = "ATEM-Tally-RX";
const char* AP_PASS = "";

// ============================================================
// EEPROM データ構造
// ============================================================

#define EEPROM_SIZE  64
#define EEPROM_MAGIC 0xC1

struct Config {
  uint8_t magic;
  uint8_t cameraNumber;  // 1-20
} config;

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);
  EEPROM.end();
  if (config.magic != EEPROM_MAGIC || config.cameraNumber < 1 || config.cameraNumber > 20) {
    config.magic = EEPROM_MAGIC;
    config.cameraNumber = 1;
  }
}

void saveConfig() {
  config.magic = EEPROM_MAGIC;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();
}

// ============================================================
// グローバル変数
// ============================================================

SoftwareSerial loraSerial(LORA_RX, LORA_TX);
E220 lora;
ESP8266WebServer server(80);

enum TallyState { TALLY_OFF, TALLY_PROGRAM, TALLY_PREVIEW, TALLY_NO_ATEM, TALLY_NO_SIGNAL };
TallyState currentState = TALLY_NO_SIGNAL;
TallyState prevState = TALLY_NO_SIGNAL;

unsigned long lastBlinkTime = 0;
bool blinkState = false;

// 受信状態
uint8_t rxBuf[LORA_PACKET_LEN];
size_t  rxLen = 0;
bool    awaitingRssi = false;
unsigned long lastPacketTime = 0;
uint32_t curPgm = 0, curPvw = 0;
bool     curAtemOk = false;
int      lastRssiDbm = 0;
bool     loraReady = false;

// LED テスト: 数秒間タリー状態の描画を止めてテスト色を保持する
unsigned long ledTestUntil = 0;
uint8_t ledTestR = 0, ledTestG = 0, ledTestB = 0;

// ============================================================
// RGB LED 制御 (コモンアノード: 255-value で反転)
// ============================================================

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(PIN_R, 255 - r);
  analogWrite(PIN_G, 255 - g);
  analogWrite(PIN_B, 255 - b);
}

void updateLED() {
  switch (currentState) {
    case TALLY_PROGRAM:
      if (prevState != TALLY_PROGRAM) {
        setRGB(255, 0, 0);
        Serial.println("[PROGRAM] ON AIR!");
      }
      break;

    case TALLY_PREVIEW:
      if (prevState != TALLY_PREVIEW) {
        setRGB(0, 255, 0);
        Serial.println("[PREVIEW]");
      }
      break;

    case TALLY_OFF:
      if (prevState != TALLY_OFF) {
        setRGB(0, 0, 0);
        Serial.println("[OFF]");
      }
      break;

    case TALLY_NO_ATEM:  // 青点滅
      if (millis() - lastBlinkTime > 500) {
        blinkState = !blinkState;
        setRGB(0, 0, blinkState ? 255 : 0);
        lastBlinkTime = millis();
      }
      break;

    case TALLY_NO_SIGNAL:  // マゼンタ点滅
      if (millis() - lastBlinkTime > 300) {
        blinkState = !blinkState;
        setRGB(blinkState ? 255 : 0, 0, blinkState ? 255 : 0);
        lastBlinkTime = millis();
      }
      break;
  }

  prevState = currentState;
}

// ============================================================
// LoRa 受信
// ============================================================

void loraSetup() {
  loraSerial.begin(9600);
  lora.begin(loraSerial, LORA_M0, LORA_M1);

  for (int attempt = 1; attempt <= 3; attempt++) {
    if (lora.configure(LORA_ADDR, LORA_REG0, LORA_REG1, LORA_CH, LORA_REG3)) {
      Serial.println("LoRa E220 configured (CH10 / SF7 / RX)");
      loraReady = true;
      return;
    }
    Serial.printf("LoRa E220 config failed (attempt %d/3)\n", attempt);
    delay(200);
  }
  Serial.println("LoRa E220 NOT responding! Check wiring (docs/lora-tally.md)");
}

// 受信ストリームからパケットを組み立てる。
// パケット直後の1バイトは RSSI（REG3 bit7 で付加させている）
void loraPoll() {
  while (lora.available()) {
    int c = lora.read();
    if (c < 0) break;
    uint8_t b = (uint8_t)c;

    if (awaitingRssi) {
      lastRssiDbm = (int)b - 256;
      awaitingRssi = false;
      Serial.printf("[LoRa RX] seq=%u pgm=0x%05X pvw=0x%05X atem=%d rssi=%ddBm\n",
                    rxBuf[2], (unsigned)curPgm, (unsigned)curPvw,
                    curAtemOk ? 1 : 0, lastRssiDbm);
      continue;
    }

    if (rxLen == 0 && b != LORA_MAGIC) continue;  // 先頭同期
    rxBuf[rxLen++] = b;

    if (rxLen == LORA_PACKET_LEN) {
      rxLen = 0;
      if (loraChecksum(rxBuf) == rxBuf[LORA_PACKET_LEN - 1]) {
        curAtemOk = (rxBuf[1] & LORA_FLAG_ATEM_OK) != 0;
        curPgm = (uint32_t)rxBuf[3] | ((uint32_t)rxBuf[4] << 8) | ((uint32_t)rxBuf[5] << 16);
        curPvw = (uint32_t)rxBuf[6] | ((uint32_t)rxBuf[7] << 8) | ((uint32_t)rxBuf[8] << 16);
        lastPacketTime = millis();
        awaitingRssi = true;
      } else {
        // チェックサム不一致: 1バイトずらして再同期
        Serial.println("[LoRa RX] checksum error");
        memmove(rxBuf, rxBuf + 1, LORA_PACKET_LEN - 1);
        rxLen = LORA_PACKET_LEN - 1;
        while (rxLen > 0 && rxBuf[0] != LORA_MAGIC) {
          memmove(rxBuf, rxBuf + 1, --rxLen);
        }
      }
    }
  }
}

// ============================================================
// 設定ページ（カメラ番号）
// ============================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ATEM Tally RX</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: 'Helvetica Neue', Arial, 'Noto Sans JP', sans-serif;
    background: #f5f5f5; color: #333;
    min-height: 100dvh;
    display: flex; align-items: center; justify-content: center;
    padding: 24px 16px;
  }
  .container { max-width: 440px; width: 100%; }
  .header { text-align: center; margin-bottom: 32px; }
  .header h1 { font-size: 1.8em; font-weight: 700; letter-spacing: 0.04em; margin-bottom: 6px; }
  .header p { font-size: 0.85em; color: #999; }
  .card {
    background: #fff; border-radius: 8px; padding: 32px;
    margin-bottom: 20px; box-shadow: 0 1px 4px rgba(0,0,0,0.06);
  }
  .section-title {
    font-size: 0.7em; font-weight: 600; color: #007cff;
    text-transform: uppercase; letter-spacing: 0.12em;
    margin-bottom: 20px; padding-bottom: 10px; border-bottom: 1px solid #eee;
  }
  label { display: block; font-size: 0.78em; font-weight: 500; color: #666; margin-bottom: 6px; }
  select {
    width: 100%; padding: 12px 14px; border: 1px solid #ddd; border-radius: 4px;
    background: #fff; font-size: 15px; margin-bottom: 18px; -webkit-appearance: none;
  }
  button {
    display: block; width: 100%; padding: 14px;
    background: #007cff; color: #fff; border: none; border-radius: 4px;
    font-size: 0.95em; font-weight: 600; cursor: pointer;
  }
  .btn-row { display: flex; gap: 10px; }
  .btn-row button { flex: 1; padding: 12px; font-size: 0.85em; }
  .btn-red { background: #e53935; } .btn-green { background: #43a047; } .btn-off { background: #666; }
  .status { font-size: 0.85em; color: #666; line-height: 1.8; }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <h1>ATEM Tally RX</h1>
    <p>LoRa Tally Receiver</p>
  </div>
  <form action="/save" method="POST">
    <div class="card">
      <div class="section-title">Camera</div>
      <label>Camera Number</label>
      <select name="cam">%CAM_OPTIONS%</select>
      <button type="submit">Save & Restart</button>
    </div>
  </form>
  <div class="card">
    <div class="section-title">Status</div>
    <p class="status">%STATUS%</p>
  </div>
  <div class="card">
    <div class="section-title">LED Test</div>
    <div class="btn-row">
      <button type="button" class="btn-red" onclick="fetch('/led?c=red')">PGM</button>
      <button type="button" class="btn-green" onclick="fetch('/led?c=green')">PVW</button>
      <button type="button" class="btn-off" onclick="fetch('/led?c=off')">OFF</button>
    </div>
  </div>
</div>
</body>
</html>
)rawliteral";

void handleRoot() {
  String html = FPSTR(HTML_PAGE);

  String options;
  for (int i = 1; i <= 20; i++) {
    options += "<option value=\"" + String(i) + "\"";
    if (config.cameraNumber == i) options += " selected";
    options += ">Camera " + String(i) + "</option>";
  }
  html.replace("%CAM_OPTIONS%", options);

  String status;
  if (!loraReady) {
    status = "LoRa: MODULE ERROR (E220 not responding - check wiring)";
  } else if (millis() - lastPacketTime > LORA_TIMEOUT_MS || lastPacketTime == 0) {
    status = "LoRa: NO SIGNAL (no packets from base station)";
  } else {
    status = "LoRa: OK (RSSI " + String(lastRssiDbm) + " dBm)<br>ATEM: " +
             (curAtemOk ? "connected" : "disconnected");
  }
  html.replace("%STATUS%", status);

  server.send(200, "text/html", html);
}

void handleSave() {
  int cam = server.arg("cam").toInt();
  if (cam >= 1 && cam <= 20) {
    config.cameraNumber = cam;
    saveConfig();
  }
  server.send(200, "text/html",
              "<meta charset='UTF-8'><body style='font-family:sans-serif;text-align:center;padding-top:40px'>"
              "<h2>Saved!</h2><p>Camera " + String(config.cameraNumber) + "</p></body>");
  delay(1000);
  ESP.restart();
}

void handleLed() {
  String c = server.arg("c");
  if (c == "red") { ledTestR = 255; ledTestG = 0; ledTestB = 0; }
  else if (c == "green") { ledTestR = 0; ledTestG = 255; ledTestB = 0; }
  else { ledTestR = 0; ledTestG = 0; ledTestB = 0; }
  ledTestUntil = millis() + 3000;  // タリー状態機械の再描画から3秒間保護
  setRGB(ledTestR, ledTestG, ledTestB);
  server.send(200, "text/plain", "ok");
}

void handleReset() {
  config.cameraNumber = 1;
  saveConfig();
  server.send(200, "text/plain", "Reset to Camera 1. Restarting...");
  delay(1000);
  ESP.restart();
}

// ============================================================
// Setup / Loop
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== ATEM LoRa Tally Receiver ===");
  WiFi.persistent(false);

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  analogWriteRange(PWM_RANGE);
  setRGB(0, 0, 0);

  // GPIO16 は内部プルダウンのみ。ボタンは 3V3 へ接続（押下=HIGH）
  pinMode(RESET_PIN, INPUT_PULLDOWN_16);
  loadConfig();
  if (digitalRead(RESET_PIN) == HIGH) {
    Serial.println("Reset button pressed! Camera -> 1");
    setRGB(255, 165, 0);
    config.cameraNumber = 1;
    saveConfig();
    delay(1000);
  }

  Serial.printf("Camera: %d\n", config.cameraNumber);

  loraSetup();

  // 設定用 AP（常時起動）
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("Setup AP: %s -> http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/led", handleLed);
  server.on("/reset", handleReset);
  server.begin();

  lastPacketTime = 0;
}

void loop() {
  server.handleClient();
  loraPoll();

  if (lastPacketTime == 0 || millis() - lastPacketTime > LORA_TIMEOUT_MS) {
    currentState = TALLY_NO_SIGNAL;
  } else if (!curAtemOk) {
    currentState = TALLY_NO_ATEM;
  } else {
    uint32_t camBit = 1UL << (config.cameraNumber - 1);
    if (curPgm & camBit)      currentState = TALLY_PROGRAM;
    else if (curPvw & camBit) currentState = TALLY_PREVIEW;
    else                      currentState = TALLY_OFF;
  }

  if (ledTestUntil != 0 && millis() < ledTestUntil) {
    // LEDテスト中: タリー状態の描画をスキップしてテスト色を保持
  } else {
    ledTestUntil = 0;
    updateLED();
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("cam")) {
      int cam = cmd.substring(3).toInt();
      if (cam >= 1 && cam <= 20) {
        config.cameraNumber = cam;
        saveConfig();
        Serial.printf("Camera -> %d\n", cam);
      }
    }
  }
}
