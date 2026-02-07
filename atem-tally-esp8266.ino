/*
 * ATEM Wireless Tally Light - ESP8266 + NeoPixel
 *
 * 配線:
 *   NeoPixel DATA -> D4 (GPIO2)
 *   NeoPixel VCC  -> 3.3V or 5V
 *   NeoPixel GND  -> GND
 *
 * 必要なライブラリ (Arduino Library Manager):
 *   - ATEMmin (by SKAARHOJ)
 *   - Adafruit NeoPixel
 *   - SkaarhojPgmspace (by SKAARHOJ)
 *
 * LED表示:
 *   赤色     = PROGRAM (ON AIR)
 *   緑色     = PREVIEW
 *   消灯     = 非選択
 *   青色点滅  = ATEM未接続
 *   紫色     = WiFi接続中
 */

#include <ESP8266WiFi.h>
#include <ATEMmin.h>
#include <Adafruit_NeoPixel.h>

// ============================================================
// ユーザー設定 - ここを環境に合わせて変更してください
// ============================================================

// WiFi設定
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ATEMスイッチャーのIPアドレス
IPAddress atemIP(192, 168, 0, 240);

// カメラ番号 (1 = Camera 1, 2 = Camera 2, ...)
const int CAMERA_NUMBER = 1;

// NeoPixel設定
#define NEOPIXEL_PIN    D4   // データピン (GPIO2)
#define NUM_PIXELS      1    // LED数
#define BRIGHTNESS      80   // 明るさ (0-255)

// ============================================================
// 内部変数
// ============================================================

ATEMmin atemSwitcher;
Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastBlinkTime = 0;
bool blinkState = false;
unsigned long lastTallyCheck = 0;

// 前回の状態を保持（不要なLED更新を避ける）
enum TallyState { TALLY_OFF, TALLY_PROGRAM, TALLY_PREVIEW, TALLY_DISCONNECTED, TALLY_WIFI_CONNECTING };
TallyState currentState = TALLY_WIFI_CONNECTING;
TallyState prevState = TALLY_WIFI_CONNECTING;

// ============================================================
// カラー定義
// ============================================================

uint32_t COLOR_PROGRAM  = 0;  // setup()で初期化
uint32_t COLOR_PREVIEW  = 0;
uint32_t COLOR_OFF      = 0;
uint32_t COLOR_CONNECT  = 0;
uint32_t COLOR_WIFI     = 0;

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== ATEM Tally Light ===");
  Serial.print("Camera: ");
  Serial.println(CAMERA_NUMBER);

  // NeoPixel初期化
  pixels.begin();
  pixels.setBrightness(BRIGHTNESS);

  COLOR_PROGRAM = pixels.Color(255, 0, 0);     // 赤: ON AIR
  COLOR_PREVIEW = pixels.Color(0, 255, 0);     // 緑: PREVIEW
  COLOR_OFF     = pixels.Color(0, 0, 0);       // 消灯
  COLOR_CONNECT = pixels.Color(0, 0, 255);     // 青: ATEM接続中
  COLOR_WIFI    = pixels.Color(128, 0, 128);   // 紫: WiFi接続中

  setColor(COLOR_WIFI);

  // WiFi接続
  connectWiFi();

  // ATEM接続開始
  atemSwitcher.begin(atemIP);
  atemSwitcher.serialOutput(0x80);
  atemSwitcher.connect();

  Serial.println("ATEM接続開始...");
  currentState = TALLY_DISCONNECTED;
}

// ============================================================
// Main Loop
// ============================================================

void loop() {
  // WiFi切断時は再接続
  if (WiFi.status() != WL_CONNECTED) {
    currentState = TALLY_WIFI_CONNECTING;
    updateLED();
    connectWiFi();
    atemSwitcher.connect();
    return;
  }

  // ATEMプロトコル処理
  atemSwitcher.runLoop();

  if (atemSwitcher.isConnected()) {
    // タリー状態を取得
    bool program = atemSwitcher.getProgramTally(CAMERA_NUMBER);
    bool preview = atemSwitcher.getPreviewTally(CAMERA_NUMBER);

    if (program) {
      currentState = TALLY_PROGRAM;
    } else if (preview) {
      currentState = TALLY_PREVIEW;
    } else {
      currentState = TALLY_OFF;
    }
  } else {
    currentState = TALLY_DISCONNECTED;
  }

  updateLED();
}

// ============================================================
// WiFi接続
// ============================================================

void connectWiFi() {
  Serial.print("WiFi接続中: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    // 紫点滅
    blinkState = !blinkState;
    setColor(blinkState ? COLOR_WIFI : COLOR_OFF);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("接続完了! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi接続失敗。リトライします...");
    delay(3000);
    ESP.restart();
  }
}

// ============================================================
// LED更新
// ============================================================

void updateLED() {
  switch (currentState) {
    case TALLY_PROGRAM:
      if (prevState != TALLY_PROGRAM) {
        setColor(COLOR_PROGRAM);
        Serial.println("[PROGRAM] ON AIR!");
      }
      break;

    case TALLY_PREVIEW:
      if (prevState != TALLY_PREVIEW) {
        setColor(COLOR_PREVIEW);
        Serial.println("[PREVIEW]");
      }
      break;

    case TALLY_OFF:
      if (prevState != TALLY_OFF) {
        setColor(COLOR_OFF);
        Serial.println("[OFF]");
      }
      break;

    case TALLY_DISCONNECTED:
      // 青色点滅 (500ms間隔)
      if (millis() - lastBlinkTime > 500) {
        blinkState = !blinkState;
        setColor(blinkState ? COLOR_CONNECT : COLOR_OFF);
        lastBlinkTime = millis();
      }
      break;

    case TALLY_WIFI_CONNECTING:
      // connectWiFi()内で処理
      break;
  }

  prevState = currentState;
}

// ============================================================
// NeoPixelカラー設定
// ============================================================

void setColor(uint32_t color) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, color);
  }
  pixels.show();
}
