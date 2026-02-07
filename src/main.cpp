/*
 * ATEM Wireless Tally Light - ESP8266 + RGB LED (コモンアノード)
 * ブラウザ設定ポータル付き
 *
 * 配線 (コモンアノード RGB LED):
 *   R（赤）      -> D5 (GPIO14)  ※220Ω抵抗推奨
 *   長い足(VCC)  -> 3.3V
 *   G（緑）      -> D6 (GPIO12)  ※220Ω抵抗推奨
 *   B（青）      -> D7 (GPIO13)  ※220Ω抵抗推奨
 *   リセットボタン -> D1 (GPIO5) -> GND (オプション)
 *
 * LED表示:
 *   赤色     = PROGRAM (ON AIR)
 *   緑色     = PREVIEW
 *   消灯     = 非選択
 *   青色点滅  = ATEM未接続
 *   白色点滅  = 設定ポータルモード (AP)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ATEMmin.h>

// ============================================================
// ハードウェア設定
// ============================================================

#define PIN_R       D5
#define PIN_G       D6
#define PIN_B       D7
#define RESET_PIN   D1
#define PWM_RANGE   255

const char* AP_SSID = "ATEM-Tally";
const char* AP_PASS = "";

// ============================================================
// EEPROM データ構造
// ============================================================

#define EEPROM_SIZE 512
#define EEPROM_MAGIC 0xB0  // 構造体変更のため更新

struct Config {
  uint8_t  magic;
  char     wifiSsid[64];
  char     wifiPass[64];
  uint8_t  atemIp[4];
  uint8_t  cameraNumber;
  uint8_t  useStaticIp;    // 0=DHCP, 1=Static
  uint8_t  tallyIp[4];     // タリーの固定IP
  uint8_t  gateway[4];
  uint8_t  subnet[4];
  uint8_t  apIp[4];        // APモードのIP (デフォルト: 192.168.4.1)
} config;

// ============================================================
// グローバル変数
// ============================================================

ATEMmin atemSwitcher;
ESP8266WebServer server(80);

void setRGB(uint8_t r, uint8_t g, uint8_t b);

bool portalMode = false;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

enum TallyState { TALLY_OFF, TALLY_PROGRAM, TALLY_PREVIEW, TALLY_DISCONNECTED, TALLY_PORTAL };
TallyState currentState = TALLY_PORTAL;
TallyState prevState = TALLY_PORTAL;

// ============================================================
// EEPROM 読み書き
// ============================================================

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);
  EEPROM.end();
}

void saveConfig() {
  config.magic = EEPROM_MAGIC;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();
}

void clearConfig() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
  EEPROM.commit();
  EEPROM.end();
}

bool hasValidConfig() {
  return config.magic == EEPROM_MAGIC && strlen(config.wifiSsid) > 0;
}

// ============================================================
// WiFi スキャン → JSON
// ============================================================

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"";
    // SSIDの特殊文字エスケープ
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += ssid;
    json += "\",\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += ",\"enc\":";
    json += String(WiFi.encryptionType(i) != ENC_TYPE_NONE ? 1 : 0);
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ============================================================
// HTML ページ
// ============================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ATEM Tally Setup</title>
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
  .header { text-align: center; margin-bottom: 40px; }
  .logo { margin-bottom: 24px; text-align: center; }
  .logo img { width: 220px; height: auto; }
  .header h1 {
    font-size: 1.8em; font-weight: 700; color: #333;
    letter-spacing: 0.04em; margin-bottom: 6px;
  }
  .header p { font-size: 0.85em; color: #999; letter-spacing: 0.02em; }
  .card {
    background: #fff; border-radius: 8px; padding: 32px;
    margin-bottom: 20px;
    box-shadow: 0 1px 4px rgba(0,0,0,0.06);
  }
  .section-title {
    font-size: 0.7em; font-weight: 600; color: #007cff;
    text-transform: uppercase; letter-spacing: 0.12em;
    margin-bottom: 20px; padding-bottom: 10px;
    border-bottom: 1px solid #eee;
  }
  label {
    display: block; font-size: 0.78em; font-weight: 500;
    color: #666; margin-bottom: 6px;
  }
  input, select {
    width: 100%; padding: 12px 14px;
    border: 1px solid #ddd; border-radius: 4px;
    background: #fff; color: #333;
    font-size: 15px; margin-bottom: 18px;
    outline: none; transition: border-color 0.3s ease;
    -webkit-appearance: none;
  }
  input:focus, select:focus { border-color: #007cff; }
  input::placeholder { color: #ccc; }
  .ip-row { display: flex; gap: 8px; align-items: center; }
  .ip-row input { width: 25%; text-align: center; padding: 12px 4px; }
  .dot { color: #ccc; margin-bottom: 18px; font-weight: bold; }
  button, .btn {
    display: block; width: 100%; padding: 14px;
    background: #007cff; color: #fff; border: none;
    border-radius: 4px; font-size: 0.95em;
    font-weight: 600; letter-spacing: 0.04em;
    cursor: pointer; text-align: center;
    transition: background 0.3s ease, transform 0.2s ease;
  }
  button:hover, .btn:hover { background: #0066d6; }
  button:active, .btn:active { background: #0055b3; transform: scale(0.99); }
  .btn-scan {
    background: #fff; color: #007cff; border: 1px solid #007cff;
    margin-bottom: 18px; font-size: 0.85em; padding: 10px;
  }
  .btn-scan:hover { background: #f0f7ff; }
  .btn-scan:disabled { opacity: 0.5; cursor: default; }
  .wifi-list {
    max-height: 200px; overflow-y: auto;
    border: 1px solid #eee; border-radius: 4px;
    margin-bottom: 18px; display: none;
  }
  .wifi-item {
    padding: 10px 14px; cursor: pointer;
    border-bottom: 1px solid #f5f5f5;
    display: flex; justify-content: space-between; align-items: center;
    font-size: 0.9em; transition: background 0.2s;
  }
  .wifi-item:last-child { border-bottom: none; }
  .wifi-item:hover { background: #f0f7ff; }
  .wifi-item .name { font-weight: 500; }
  .wifi-item .info { font-size: 0.75em; color: #999; }
  .wifi-item .lock { font-size: 0.7em; color: #bbb; margin-left: 6px; }
  .toggle-row {
    display: flex; align-items: center; justify-content: space-between;
    margin-bottom: 18px;
  }
  .toggle-row label { margin-bottom: 0; }
  .toggle {
    position: relative; width: 44px; height: 24px;
    background: #ddd; border-radius: 12px; cursor: pointer;
    transition: background 0.3s;
  }
  .toggle.on { background: #007cff; }
  .toggle .knob {
    position: absolute; top: 2px; left: 2px;
    width: 20px; height: 20px; background: #fff;
    border-radius: 50%; transition: left 0.3s;
    box-shadow: 0 1px 3px rgba(0,0,0,0.15);
  }
  .toggle.on .knob { left: 22px; }
  .static-fields { display: none; }
  .btn-test { flex: 1; padding: 12px; font-size: 0.85em; border-radius: 4px; }
  .btn-red { background: #e53935; } .btn-red:hover { background: #c62828; }
  .btn-green { background: #43a047; } .btn-green:hover { background: #2e7d32; }
  .btn-off { background: #666; } .btn-off:hover { background: #444; }
  .footer {
    text-align: center; margin-top: 24px;
    font-size: 0.72em; color: #bbb; letter-spacing: 0.02em;
  }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div class="logo">
      <img src="data:image/webp;base64,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" alt="Symphonity">
    </div>
    <h1>ATEM Tally</h1>
    <p>Wireless Tally Light Setup</p>
  </div>
  <form action="/save" method="POST">
    <div class="card">
      <div class="section-title">Network</div>
      <button type="button" class="btn btn-scan" id="scanBtn" onclick="scanWifi()">Scan WiFi Networks</button>
      <div class="wifi-list" id="wifiList"></div>
      <label>WiFi SSID</label>
      <input type="text" name="ssid" id="ssidInput" placeholder="Network name" required value="%SSID%">
      <label>WiFi Password</label>
      <input type="password" name="pass" placeholder="Password" value="%PASS%">
    </div>
    <div class="card">
      <div class="section-title">Tally IP</div>
      <div class="toggle-row">
        <label>Static IP</label>
        <div class="toggle %STATIC_CLS%" id="staticToggle" onclick="toggleStatic()">
          <div class="knob"></div>
        </div>
      </div>
      <input type="hidden" name="useStatic" id="useStaticVal" value="%USE_STATIC%">
      <div class="static-fields" id="staticFields">
        <label>Tally IP Address</label>
        <div class="ip-row">
          <input type="number" name="tip1" min="0" max="255" placeholder="192" value="%TIP1%">
          <span class="dot">.</span>
          <input type="number" name="tip2" min="0" max="255" placeholder="168" value="%TIP2%">
          <span class="dot">.</span>
          <input type="number" name="tip3" min="0" max="255" placeholder="0" value="%TIP3%">
          <span class="dot">.</span>
          <input type="number" name="tip4" min="0" max="255" placeholder="100" value="%TIP4%">
        </div>
        <label>Gateway</label>
        <div class="ip-row">
          <input type="number" name="gw1" min="0" max="255" placeholder="192" value="%GW1%">
          <span class="dot">.</span>
          <input type="number" name="gw2" min="0" max="255" placeholder="168" value="%GW2%">
          <span class="dot">.</span>
          <input type="number" name="gw3" min="0" max="255" placeholder="0" value="%GW3%">
          <span class="dot">.</span>
          <input type="number" name="gw4" min="0" max="255" placeholder="1" value="%GW4%">
        </div>
        <label>Subnet Mask</label>
        <div class="ip-row">
          <input type="number" name="sn1" min="0" max="255" placeholder="255" value="%SN1%">
          <span class="dot">.</span>
          <input type="number" name="sn2" min="0" max="255" placeholder="255" value="%SN2%">
          <span class="dot">.</span>
          <input type="number" name="sn3" min="0" max="255" placeholder="255" value="%SN3%">
          <span class="dot">.</span>
          <input type="number" name="sn4" min="0" max="255" placeholder="0" value="%SN4%">
        </div>
      </div>
    </div>
    <div class="card">
      <div class="section-title">Switcher</div>
      <label>ATEM IP Address</label>
      <div class="ip-row">
        <input type="number" name="ip1" min="0" max="255" placeholder="192" required value="%IP1%">
        <span class="dot">.</span>
        <input type="number" name="ip2" min="0" max="255" placeholder="168" required value="%IP2%">
        <span class="dot">.</span>
        <input type="number" name="ip3" min="0" max="255" placeholder="0" required value="%IP3%">
        <span class="dot">.</span>
        <input type="number" name="ip4" min="0" max="255" placeholder="240" required value="%IP4%">
      </div>
      <label>Camera Number</label>
      <select name="cam">
        <option value="1" %SEL1%>Camera 1</option>
        <option value="2" %SEL2%>Camera 2</option>
        <option value="3" %SEL3%>Camera 3</option>
        <option value="4" %SEL4%>Camera 4</option>
        <option value="5" %SEL5%>Camera 5</option>
        <option value="6" %SEL6%>Camera 6</option>
        <option value="7" %SEL7%>Camera 7</option>
        <option value="8" %SEL8%>Camera 8</option>
        <option value="9" %SEL9%>Camera 9</option>
        <option value="10" %SEL10%>Camera 10</option>
        <option value="11" %SEL11%>Camera 11</option>
        <option value="12" %SEL12%>Camera 12</option>
        <option value="13" %SEL13%>Camera 13</option>
        <option value="14" %SEL14%>Camera 14</option>
        <option value="15" %SEL15%>Camera 15</option>
        <option value="16" %SEL16%>Camera 16</option>
        <option value="17" %SEL17%>Camera 17</option>
        <option value="18" %SEL18%>Camera 18</option>
        <option value="19" %SEL19%>Camera 19</option>
        <option value="20" %SEL20%>Camera 20</option>
      </select>
    </div>
    <div class="card">
      <div class="section-title">Advanced</div>
      <label>Portal AP IP Address</label>
      <div class="ip-row">
        <input type="number" name="ap1" min="0" max="255" placeholder="192" value="%AP1%">
        <span class="dot">.</span>
        <input type="number" name="ap2" min="0" max="255" placeholder="168" value="%AP2%">
        <span class="dot">.</span>
        <input type="number" name="ap3" min="0" max="255" placeholder="4" value="%AP3%">
        <span class="dot">.</span>
        <input type="number" name="ap4" min="0" max="255" placeholder="1" value="%AP4%">
      </div>
      <p style="font-size:0.72em;color:#999;margin-top:-10px;">Next portal access will use this IP</p>
    </div>
    <div class="card">
      <div class="section-title">LED Test</div>
      <div style="display:flex;gap:10px;">
        <button type="button" class="btn btn-test btn-red" onclick="testLed('red')">PGM</button>
        <button type="button" class="btn btn-test btn-green" onclick="testLed('green')">PVW</button>
        <button type="button" class="btn btn-test btn-off" onclick="testLed('off')">OFF</button>
      </div>
    </div>
    <button type="submit">Save & Restart</button>
  </form>
  <p class="footer" id="footerIp">ATEM-Tally &mdash; %FOOTER_IP%</p>
</div>
<script>
function scanWifi(){
  var btn=document.getElementById('scanBtn');
  btn.disabled=true;btn.textContent='Scanning...';
  fetch('/scan').then(r=>r.json()).then(d=>{
    var list=document.getElementById('wifiList');
    list.innerHTML='';list.style.display='block';
    if(d.length===0){list.innerHTML='<div class="wifi-item"><span class="name">No networks found</span></div>';btn.disabled=false;btn.textContent='Scan WiFi Networks';return;}
    d.sort((a,b)=>b.rssi-a.rssi);
    d.forEach(w=>{
      var sig=w.rssi>-50?'Strong':w.rssi>-70?'Good':'Weak';
      var div=document.createElement('div');
      div.className='wifi-item';
      div.innerHTML='<span class="name">'+w.ssid+(w.enc?'<span class="lock"> [lock]</span>':'')+'</span><span class="info">'+sig+' ('+w.rssi+'dBm)</span>';
      div.onclick=function(){document.getElementById('ssidInput').value=w.ssid;list.style.display='none';};
      list.appendChild(div);
    });
    btn.disabled=false;btn.textContent='Scan WiFi Networks';
  }).catch(()=>{btn.disabled=false;btn.textContent='Scan WiFi Networks';});
}
function testLed(c){fetch('/led?c='+c);}
function toggleStatic(){
  var t=document.getElementById('staticToggle');
  var v=document.getElementById('useStaticVal');
  var f=document.getElementById('staticFields');
  if(t.classList.contains('on')){t.classList.remove('on');v.value='0';f.style.display='none';}
  else{t.classList.add('on');v.value='1';f.style.display='block';}
}
(function(){
  if(document.getElementById('useStaticVal').value==='1'){
    document.getElementById('staticToggle').classList.add('on');
    document.getElementById('staticFields').style.display='block';
  }
})();
</script>
</body>
</html>
)rawliteral";

const char HTML_SAVED[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ATEM Tally - Saved</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: 'Helvetica Neue', Arial, 'Noto Sans JP', sans-serif;
    background: #f5f5f5; color: #333;
    display: flex; align-items: center; justify-content: center;
    height: 100dvh;
  }
  .msg { text-align: center; }
  .saved-logo { margin-bottom: 24px; text-align: center; }
  .saved-logo img { width: 220px; height: auto; }
  .check {
    width: 56px; height: 56px; border-radius: 50%;
    background: #007cff; color: #fff;
    display: flex; align-items: center; justify-content: center;
    margin: 0 auto 20px; font-size: 28px;
  }
  h1 { font-size: 1.5em; font-weight: 700; color: #333; margin-bottom: 10px; }
  p { color: #999; font-size: 0.9em; line-height: 1.6; }
</style>
</head>
<body>
<div class="msg">
  <div class="saved-logo">
    <img src="data:image/webp;base64,UklGRjgaAABXRUJQVlA4WAoAAAAwAAAAVwIAawAASUNDUMgBAAAAAAHIAAAAAAQwAABtbnRyUkdCIFhZWiAH4AABAAEAAAAAAABhY3NwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAA9tYAAQAAAADTLQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAlkZXNjAAAA8AAAACRyWFlaAAABFAAAABRnWFlaAAABKAAAABRiWFlaAAABPAAAABR3dHB0AAABUAAAABRyVFJDAAABZAAAAChnVFJDAAABZAAAAChiVFJDAAABZAAAAChjcHJ0AAABjAAAADxtbHVjAAAAAAAAAAEAAAAMZW5VUwAAAAgAAAAcAHMAUgBHAEJYWVogAAAAAAAAb6IAADj1AAADkFhZWiAAAAAAAABimQAAt4UAABjaWFlaIAAAAAAAACSgAAAPhAAAts9YWVogAAAAAAAA9tYAAQAAAADTLXBhcmEAAAAAAAQAAAACZmYAAPKnAAANWQAAE9AAAApbAAAAAAAAAABtbHVjAAAAAAAAAAEAAAAMZW5VUwAAACAAAAAcAEcAbwBvAGcAbABlACAASQBuAGMALgAgADIAMAAxADZBTFBIIBEAAAGgxv//4qb5Xe6StKWhXooUd2esuDuUeZHOfegEd527uws6V2BjOEXHKF5GKYNiCxXaRu/u96DJ7/+/u39G8ywiJgBMLCW1H3Lv8s+35J+9XO69fPn49q9fmDiqa4YEZo5p3m/8ws82HrnkR0QsKdr/3YsTsrukSxDFt6XNOYKcy97oLEumkByjf1GR8+mlmTYpKpf04M+FOhqoX9y6pLFRUp/391egkfqZdRMSom0Joz6uRDNuvDuTn9Tu8cNoRv+Xo2pHzyTnpKto3g9dEhdb/R1o3sAke5Qs/a2zaOqK9ddzuGWXhqYufq9eFCxjThmaXvugvUSyDVyH5r88Jy3KJd9QidZ82hlOqvUjWjMwWo5mtd0ZRKsWjAplm3YJrRrc2ChqZZ9Sjlb+oC4AtFiHVvZOlKNTqXlo8fNN5VEqWnxHUjRq8L9o+aodAbR8YZ+ok/LgVRRh0HpYfo8cXVKWoCCDuuVQm6pEk+QvNVGgqlkO9ZejSIkrdBSnqlkO8eOYaJH9UxRqULMevmePDtm+0MWCqm49fD4qJC/RULSaALQZtijQnShgr/Uw0C/606tCRKhaD6uyroXiE4muSFQrkaoIqNYJFLKuWg93xVwD/YrEPZFoFVIHicexDgWt6tbDb22mkuIyu4+f/PLLLy97+KYejWsrNX93obBVAQR6mKnN96crkOg5d2xB3Rq+phfEhar1sLyxWZSx65DnxjlpNTDOWKLdWtI6FLimWw9fNkn9POStTUuValiUg0h811pZQZFhQADBbqYYWIIGXrqrhi75Agpd16yHe50mGOdBY7e0qZF7CAUvArWfcU2votG+sTVw9mLRoW49dCtGJZaj8ctq4Oai8DUB4F1GLUfjzyXXXLx3kdpHKPJx8aEmgL2SMWlovKcV1FzwFMUw/X+RPsKY9xjKd78y67kNxRrtOah5c57BCKgKADfaDNlK2jEcQqeMeb48XH5MDVx3jIheAZSnGGGrpBQkAFV+rlivVtUKAEByUBUODjLYHUQllK1Ov5tzc3NHda8tMTibDLolNzf31oGN7TTZQQ0luTqNHJubO25k10TJBEqDwbfl5o6/oVuybIDkajP45tzc3PE39Gkcw0NxEOVQSsOhObm5Y7O7JdsYHEQ7gM3hcMTlU953hLUDyA6qjYODqgDAssigCQAfNCIdiYFawOhaGkTExVC9o5u6gc3upv4K/7iJX4Rod/mqjtXV0vWpFOc8dxWG1KvckyTK425qiMRvSwIYMlh6ppdkVLMTFRhSLfurkcRJ6Xq8xIehtaorPzWQmL5xE+dUk64rrMSQatnuuiSXm3gK4GG32+0OUrzusAdqwQNu6nK2Jm7qmwC2osiAqgAuSwa0opwD9pbvqb9ByKTzSCxOZLoTqQ9CCRJ/AICUn5Ds/TgpVPyHV5B+NlcKNxepAFD7Mz/SD093GNH5DyTrv7bmUe/Ns8iq582NY/gdiUsBoN8mJOs/NyXURmIJwGPI/XQ8NK1E4iEH00ykZgMMwggZFAB2N6Ad5QwHgAmtQ0nrKFo7pncoeibTMA1ZT7Ws1v00sn8kc+qUj+ybO3KzZVchq7u/xJK4APn+l+Pkp9zgRdbiTlaQ91MqMpg2UIKpAB9GCl0ED5ujigsxg4IrmC5TioBlUhDZiwGgaxB57pG4dPQgz0BnXrfryB7ozdDkInLf6uQ2WUd2XxcLwA0UfJTpP8rbALA3YgQE8IcBbSn4mmSEtJMSkBkykKg3YRlWiTw3ymORrzaex1APcs7h82AQeZ6tR7o3gAZe6MTpEQ15Hk+zABRSztsZbkOiJxXAVh4pMCiAcolfJklfEGcATKfg7QzrKSdjGA4Gkav+HHKvz6EQuY/gUVCFfFcS5BlocD8ua6qQ74dWeImCWTTbbkoeAAzEiKkJAPvwiyMhXu0j86tNWkazF1N+BQbzP8/BwAoXB+5a7XATdaOCrXhw98gW6EmaRWvso8wEgI8jR1AEb/ODEzTEy98sSpP4SGsoF2ldVYJW32qnJRPhPodpcG2Ym3Q0/GKyeXCpBWAfZTtthE7wJgPI/0UOXQTrDfiOpfrpe7LTbGzQViNgO9IjSNwLVsNUbkGfL6Cz4N3mKXGFaFiKzJrOhD/GmMcba4HRGgFbkj5D4icAkI7/q4/G8OvLAxHVqhvtTPYjlJ0Um06ZwEnza7z8frYnOXnuUBRFSdjBUiJx0v1BpkDnatImpOulUxVF6X9EY8CHeal+lSnY2gJ1SyhvUmx+ynAA6BxJygRQmsjPtpYPInp/XeSkwUuUc4mELkjU6/A4N7YVAHQd52PbmJ0AED/6d4ZTEo+ym+wQuvUmGj7Aw/tQRwBoetsFGo6u1j5A0l9qC6HTJ1TSCh08vPe2BbC1vPM/mj6MR3XlIOVdYJS+pxxyEnKRqCcCwJBIIkJfCj/orPFCRHVxB4kSqxK01oSXKbuBw3YnhB58keVxCDuTVmjjcLYLUF+m7bCxHbBD6OTttPsAQDmL1PI+QE3ZS8LpHP5UIHTmURLebwFo7Sd46hE+o7wEADA2klQJQEs3ABro/BBxjY0AbxJwFeFPgp7OoxaEz2FYAsTTNJmtKhPoX5DUeKagE8LHnCNNAoBbkaqlAGMx6ZCD6TxQL5IWWAHyCDiLUEDwK9XmRhJVANjfCBiuGoElgwl3UYJSGCcST8RyuA2INpXkzaSkB4y5GxiT3RTMYroXqE+TXgKAV0kPAmvaZUqgBdN40guWm04pcYZprRJ2StVejyRCXG4IdNluBOKT4Rw6AXPDLKF8BWxVdgqMJJ2XKMp+Qw5LLHCfTtnBotcjOQIsFyn7FSZ4QCPgLKZUUnPdao38BOwZZhUSZ0L1NRElIIDPjQGY5jMCn5RCwQTKolC2MwQdOGyXSAppDpA3GjIf2C9R0M6ikOAcQzpS+wPHfIqH5SLQ/7UarKbMDZVQSdDiQqyPKH4BHDAKUm48ZYC/Q5jGVYTLoVp4CFt4/AA0nZJB+8GQhhzeJsUznAD6rwwLKBeTeSykYCrDWwx7LZcVIOwLlaUSPoGQmyKJHgkAoOXHu3ROWBkfypZPwI4hspH4iKnstBcNUTjMJdVl2MPwOcMqSqHMYzipFcNShjzLuc4RsHWIBUgc8f+swAzVb7xQpfPAZaFgLGVniEJKipAKgONA0nUm+pnyA/B0kAYKDZ6mvFlNKiJoiRFIiEfNAhA/IHsnh31h5FLCuQQASEHidhDSGh4tSN1NtI7yLBeooAwTm6uUcMgBAD2Q+AKIQ40oB8xT3T7vexZ/GHiWoDUHgGkEPUNMa3k0EVR5xIBvCJ4MAHieUJUQ5nsB+CLKXnMBgH2jn4RZYXoRcBUAfEA4GSemfB6DSVnW+IWL5KMMFdwdBJwDALsIebYwKwXgjyifmQ6gK+25MLYSQhBAriT8DGJCmcNcUjMTfUcplHk0Q2prwcVeIJQ5weEjzICwLwkgsj5mAWgfpKwPA8MImAv9MLyWLqoMDq+Q4k30NuVCIo8HSGmCg7kE7AlzMPzV1HAzRaBHkgGGxL3q5OPIp+SFq+MmLJX+IeSBqKZzOEhymmgEBXvzeIMSVETX2kuYo1whfAzh7xBBRQTR0gz5Rj/UgYttJxdpL6GkjpswRVgb2TpqlAIwkVxFOWjjUEJ5BkRnP0zY39pHGEUYKoJI6k0xQH4GEX2P87AfoCwLBy0IOBPDaw2EhTksrn+Rmm0m2ErBO5jkb5GodRAe5BDwSQzvTyJ0EIEvglxJNOBhDFneSWHKDFI6E+AvAvUoiKsokeEOpKrJpnqQ5Mlk6R+knIgVyQZOUETQCc8DMV0EegQ56OB3ky8UBne2ZIg/jsQAUJfx0FsIDA/FkyappF2yqepoFHS3puV4kPq6JJJTMZzeJBArUyjSZQFoeuT4BbinnkaivvWx68Mpd+UjtYDUkMfJOJHhzuHhkparSF4CpoKlJAzMsIdLWORH6qUksJq8n+K/J1T2PJnWl8cumQLv/696jVuSD1m9Re/de1vOwr/9SH+GBJs5fA9CQ21DszqxMYkNH/AivcJpsqSLJMSKcY3qJCRmNF7qR/oMsBzspCBu6tr05p+8eEShQT6HmUDuLgBUI0dzXtImNKfelHYdm5YsOESt7OSJ835knQ4mgwcYEIOl54rLVWS8FCeAlbSwTDex+Vw0qUQA3ohxBXj38JnkLaDHn2HaBsLje9llOljNwtXdDASw2Az1rzJ9Aoy7BRDUI8VKbtD9kimu1GOA1UyTIpK3IZgv/pRhnptABG11E8A6plEsbwsAtUjxAD9I3WECtSOwxrPoyRFppmQBSC0w6m5JCHDEDI2ZElj6iCAYKdoZAPZHDdNygX0tw58gshXcXpHACpCxxZCyOyUQw1QzwG6G54FVKhSAFiH+AWNjfqo0pLwvcJxM0zKFJv2kcfG9JIE1IGZzkF9xGyBaLLbYDPNongQmmCcA3RMZ+hsEtharDNhQX+IRSzsZJzSw36Xz6CuBVUDuUclrSRIIA5pdMUE92h6Z7ToBoBoRLicYBQDNFu3mov94qwJ8F5NWgNgABm5m8bzaBKimA0hZfIHHZ9dLIBDotY/l9BQbE6wmzQR2W5EI9EjwA5gzbdnfhV5S6eHPGgL3NkGCFst0sIj4AUMRVaHNLyJu5wLKw4crw/mOvZUE9E+LiD8yvFxEXEADkHMOXtIpnmOfpADrV0XEaQzfFRGnVosvIuZTwDnvuDec+8BdDuDYSyV4anOAXgLAygigNTMJgBzfoPPQ+1esWLHi8wlDOqbbwcD2lE3AHO8ixtLARQW600WsBVwA7PV7PfDFihUfjeuZ6QTWWBcxjiHGRXSyAMjJ7W/6oAgRfb9N7pnpBPY4F9HJEOciOqpJLmI8CSCmUffRc55cNHlY5zQFuA7SCJ8CT9shAQQ18f0E4oxbNzVEixNInMQmQB7XkBkb7pWrdSlG4mguMFsAGBTfLQLpg+j9Orv30jIkakk1K/fqePWTYf3erkJiRRIf5xkBBIR3VhaH/B9yfB1qVJylyHEqcL5bALpPcL72IM6bkWNpes3KJOR4Jo5X/AXrYVAX2++yQFbyWAk1K7t5LAPuTVTrYUBo3jQQZx2dQ4WjZmWEzuEvhZ+0SgC6JrJlIE7pD2TXR0ONSsxB5NgLDEy/ZD30C+x0rEAyr7IFZ0DNSlc/m3ccGJojAPQLy9cEBJp8hm2mrYallZttvGSM/QsB6KqoFkoiAWXo+z7SlkEg6msXiLlljUrRf8oCox0HrYdBXUzrQLjJ963PO1RwriB/x+dZNhD2Pbuo1xgAkDHlj12HCooL/t72XgcJjG+kWg8DQqpKFg8A2BS7067YQOSSTL32AJBkxe60KzYw520CQFVAVzpC9Fea5bOergmnMheiwdJb1kNURTMBosPKmwLQNaEEJ9iiROD8UrUc6rpAfIttEDWW3rEeoiqOqRBNVqYJQNcEUZEjRZVAGl9iOcSALoKTfSDq3PCC9bQ1mvUKkyEKnfGhbrH8DlLWKYup8+MgKm2b+K+VAq/HAYDzba+VDo2CqHXSHs0q+r/NIXTnCt0qwc8cEMWWRxyxRnByIoR3PV5hjfVZEOV2zT5lvivvZQC94acl5tt7px2i4LMqdVN516YAe8JXXlOpF7MhSp429oB5rs5oKgFPW+Np502j/TAkHqLn8sCP/jHDle/ucgL/Wrd/c8kE6r5n20kQbe/1Q4nPiGD53nEyGK3cur0sYIBWdemVBhCdT2g7/qMzfCq+n9C1jg3MKKV2uu+rK1wCe54Z3DQGovlSnYEPPfXBt+vz9h45kp+36edPX3o8uzmYvf6gB5e/9/Vvm/Ly8vI2/Pj5azNzOsdCdBpWUDggIgcAAPA4AJ0BKlgCbAA+bTKWSCQioqEjdArwgA2JZ25v8uuiAmsE82mZ0DOPmIAfzfGjny3+k/jN3gHES2H/klf2Z3zEJv3bVF+0fjv+3f+Z5AIyz6lfwf5Pf4rtAeYB+nH9w/ifWN8wH6ff8b/acIB/K/4/6QHtAegB+w3pOfsZ8Fn63fsV8Bv6s/9jNSfXD6z7WHZLLAibMjKTfypD0///5MP4L/976gIcy71UOq7CxpgaIERMLwloEhvtpHJkPRIeiQ9Eh6JD0SHokGKim8RKjz6ss6rrZl3TaBAuVoYuIrd3sKIoqJRiu+fCFyfyXpvRkD2NKSqx7wL/luI0tSsQim7mE5RioKlEC4XBfmmQABkao+X5Oojm0zRcmPEUQilkhKzykaUn3sDtWANgLJgfj6aoXTSm6b68U+g0u+Hm0ZuPmeUtARSW78+fePufBD60BHzuMrMQXvwAYTRu67RfsCOxrWbl/z9x9ypgTa4zNj9nseTT9Z3vRYStzln8Z3qswTgIvIWAywZ407AbkUZgf6m3w2QPTFt7hx+DVTDuVD7tcV5v7b46X8V6iVZ8T3fRavKvp8JeVTU0Fxg7LiiOLywjvjFfGK+MV8Yr4xXxivgAAP2sVgvLtHUPPe9HnOhlQBFudipKtD9/kRRejXnxnl3vAE/YYujqxxYNb/pgn8oM+VlmaTWbIFU3c0ExxZSSbXGWpQ8PrYKi+I9llLpvn6I3SUBE8qmSHfXPGgStAA0H1VYdEr6U4BllQvkh7JMQFNR0FLfPVpDvn+cbVVDLfEtq9i6pm5r4VuF57Zpr0b3/7CoB2oaZr91gKEMfkPpWxs/eMx+C4ClPjoomzTw0ur+4AAAALdG/ZXzoTEtyAYXFkxjJBYXF2i1Pxo7/9YxHWDQ0Y1AFtkIoNAH/yz15TCYTrke7yzk9UD0/81j0hiHV3msI1CfCjUcD29Zww9YpBxk/OgQTiOMliDIuzkYP2Aj9uFYVcrQBeBbT//norBXQYZULoFaFT+Ytdfvub50PJxU0lb///HiL9/djuwVrh+ZNmMKEAdKf/DxndKLy2GE302efuuAyzE3sl8djYs4x1Mvk51IUlbFyBy0UlMlFKpBEk+8rDGs/j6GeTBqB0wYJApNkV2+h+gGxFQ6fN3hGm93SfFRca6tFGHyLsDAX8Bz6IV520VigKK3B4PG+MQbIUi/KHRysUo+wt+4pYlTAhqQPP3p8DWi6/FitH7rLCJVgB1FoT9+w5SjtfCgoZo6ea9YXewrssH3ioLPzfEfHcwdsYkPzLeB9KxdCrldZMDnvpHJyWIIn1MPgqbBGPo8teGHlbMsRh4WjTv5DeAIqw+E16mrAv/vwOWYlfjHsy38VI0ZeZ4lFfp/ztHGWQPvpLwjJ+ltL7wjnM+a7rDeoOYhRH3PUZU7vQ5lYfKpW+fzYisYz9InuVu543KX+Y7vZSJejROiIUfCRNKkWrpEPqXQ6AFp7gzwBi7QrwR2K+aAsKxJF3c+pCd/4Mwv3D5Lwwc/vZxvl//LuW9g/1lm1TfAs5r5vWB3OOvxUVrlL/ias1P7/+G8wIZOeW5C6A9XJCLibA2iiWySwpWS3oG4I1RhAf8IHxIYfwW5pnOU+mLsn5V7yF2xPZEq+aqot8YAeWCqxF6HZ4Y667xpe2vj4dbMsMYA8uLJd0ropd9e9h9WtDLiaNwzOO+497IXSsVtV+BwRjGaNZpqj5/WSxhB0VwZDEkWzu0vAYJGx8gjwYXnJtne1jWOuZRa7uNiLk3wpPNFbBznHo8O8grzOKmWXUd+fYS+k4Y7UAeNnTsxShd1BFnxqmzwGvboP7QbFaK1Gi/+es7Cmoa98jUooHo55r02zKbtjgeJWQrudT1iBQK7YsHcmxu3btMWxJzdKLy2gHl6G6lob0a73crsZQv//uu7aSym0GFTJVCaObipjtgM+dLFGxwTeR1kTDL3qrSGUBafhNQWaR7pkr/qry8t4Nbl92OuMLT6qEE1zRRZ08nanB0H3mODf1xux4culGi1ZD5cFw8y+akmM1sARdZ8vVK/rOqsekVvMfFRNt6omz9DKmu/7RXb1hRwF9hzfdC1DnGM4+pTmc91U7mbNp3zst4sAZoNkeP7iTgXz6umMgeMgIBxMQX3DczIm8/1edTi2WhtMFt/gDQhtwy6jP0sz/qVECPWDacn1GgFMfZmNvYwNJeBWHnykRUfQ5+xvOaIbdNuQOzWLVN/ttwAAFbfNwwTQICcUR8Ymh2RgyGiik8mFM5Z8yyW++NcOfA2OBY+xnYf6GEn+avZVbRqkJMrvDCZgbt3+tGo2/TdMBXaidts8v9dues+0kVQufVBqehEt6AbKyP3wk3W25dxlNn+LsipI7wUecfJ4QsDRYDpCTGR19nO6ghu77p1/dCYivgky3nPEZPN7eYyCE6/YAAAAAAAA" alt="Symphonity">
  </div>
  <div class="check">&#10003;</div>
  <h1>Saved</h1>
  <p>Restarting...<br>The tally light will connect to your network.</p>
  <p>If it fails, "ATEM-Tally" AP will reappear.</p>
</div>
</body>
</html>
)rawliteral";

// ============================================================
// Web Server ハンドラ
// ============================================================

String buildPage() {
  String html = FPSTR(HTML_PAGE);

  html.replace("%SSID%", hasValidConfig() ? String(config.wifiSsid) : "");
  html.replace("%PASS%", hasValidConfig() ? String(config.wifiPass) : "");

  // ATEM IP
  html.replace("%IP1%", hasValidConfig() ? String(config.atemIp[0]) : "192");
  html.replace("%IP2%", hasValidConfig() ? String(config.atemIp[1]) : "168");
  html.replace("%IP3%", hasValidConfig() ? String(config.atemIp[2]) : "0");
  html.replace("%IP4%", hasValidConfig() ? String(config.atemIp[3]) : "240");

  // Static IP toggle
  bool isStatic = hasValidConfig() && config.useStaticIp == 1;
  html.replace("%USE_STATIC%", isStatic ? "1" : "0");
  html.replace("%STATIC_CLS%", isStatic ? "on" : "");

  // Tally IP
  html.replace("%TIP1%", isStatic ? String(config.tallyIp[0]) : "192");
  html.replace("%TIP2%", isStatic ? String(config.tallyIp[1]) : "168");
  html.replace("%TIP3%", isStatic ? String(config.tallyIp[2]) : "0");
  html.replace("%TIP4%", isStatic ? String(config.tallyIp[3]) : "100");

  // Gateway
  html.replace("%GW1%", isStatic ? String(config.gateway[0]) : "192");
  html.replace("%GW2%", isStatic ? String(config.gateway[1]) : "168");
  html.replace("%GW3%", isStatic ? String(config.gateway[2]) : "0");
  html.replace("%GW4%", isStatic ? String(config.gateway[3]) : "1");

  // Subnet
  html.replace("%SN1%", isStatic ? String(config.subnet[0]) : "255");
  html.replace("%SN2%", isStatic ? String(config.subnet[1]) : "255");
  html.replace("%SN3%", isStatic ? String(config.subnet[2]) : "255");
  html.replace("%SN4%", isStatic ? String(config.subnet[3]) : "0");

  // Camera (1-20)
  uint8_t cam = hasValidConfig() ? config.cameraNumber : 1;
  for (int i = 1; i <= 20; i++) {
    String key = "%SEL" + String(i) + "%";
    html.replace(key, (cam == i) ? "selected" : "");
  }

  // AP IP
  bool hasAp = hasValidConfig() && config.apIp[0] != 0;
  html.replace("%AP1%", hasAp ? String(config.apIp[0]) : "192");
  html.replace("%AP2%", hasAp ? String(config.apIp[1]) : "168");
  html.replace("%AP3%", hasAp ? String(config.apIp[2]) : "4");
  html.replace("%AP4%", hasAp ? String(config.apIp[3]) : "1");

  // Footer IP (現在のAP IP)
  IPAddress currentApIp = WiFi.softAPIP();
  html.replace("%FOOTER_IP%", currentApIp.toString());

  return html;
}

void handleRoot() {
  server.send(200, "text/html", buildPage());
}

void handleSave() {
  memset(&config, 0, sizeof(config));

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.toCharArray(config.wifiSsid, sizeof(config.wifiSsid));
  pass.toCharArray(config.wifiPass, sizeof(config.wifiPass));

  config.atemIp[0] = server.arg("ip1").toInt();
  config.atemIp[1] = server.arg("ip2").toInt();
  config.atemIp[2] = server.arg("ip3").toInt();
  config.atemIp[3] = server.arg("ip4").toInt();
  config.cameraNumber = server.arg("cam").toInt();

  config.useStaticIp = server.arg("useStatic").toInt();
  if (config.useStaticIp == 1) {
    config.tallyIp[0] = server.arg("tip1").toInt();
    config.tallyIp[1] = server.arg("tip2").toInt();
    config.tallyIp[2] = server.arg("tip3").toInt();
    config.tallyIp[3] = server.arg("tip4").toInt();
    config.gateway[0] = server.arg("gw1").toInt();
    config.gateway[1] = server.arg("gw2").toInt();
    config.gateway[2] = server.arg("gw3").toInt();
    config.gateway[3] = server.arg("gw4").toInt();
    config.subnet[0] = server.arg("sn1").toInt();
    config.subnet[1] = server.arg("sn2").toInt();
    config.subnet[2] = server.arg("sn3").toInt();
    config.subnet[3] = server.arg("sn4").toInt();
  }

  // AP IP
  config.apIp[0] = server.arg("ap1").toInt();
  config.apIp[1] = server.arg("ap2").toInt();
  config.apIp[2] = server.arg("ap3").toInt();
  config.apIp[3] = server.arg("ap4").toInt();

  saveConfig();

  Serial.println("Config saved!");
  Serial.printf("  SSID: %s\n", config.wifiSsid);
  Serial.printf("  ATEM: %d.%d.%d.%d\n", config.atemIp[0], config.atemIp[1], config.atemIp[2], config.atemIp[3]);
  Serial.printf("  Camera: %d\n", config.cameraNumber);
  if (config.useStaticIp) {
    Serial.printf("  Tally IP: %d.%d.%d.%d\n", config.tallyIp[0], config.tallyIp[1], config.tallyIp[2], config.tallyIp[3]);
  } else {
    Serial.println("  IP: DHCP");
  }

  server.send(200, "text/html", FPSTR(HTML_SAVED));
  delay(2000);
  ESP.restart();
}

void handleLed() {
  String c = server.arg("c");
  if (c == "red") { setRGB(255, 0, 0); }
  else if (c == "green") { setRGB(0, 255, 0); }
  else { setRGB(0, 0, 0); }
  server.send(200, "text/plain", "ok");
}

void handleReset() {
  clearConfig();
  server.send(200, "text/plain", "Config cleared. Restarting...");
  delay(1000);
  ESP.restart();
}

// ============================================================
// 設定ポータル起動
// ============================================================

void startPortal() {
  portalMode = true;
  currentState = TALLY_PORTAL;

  WiFi.mode(WIFI_AP_STA);  // APモードでもスキャン可能にする

  // カスタムAP IP設定
  if (config.apIp[0] != 0) {
    IPAddress apIp(config.apIp[0], config.apIp[1], config.apIp[2], config.apIp[3]);
    IPAddress apGw(config.apIp[0], config.apIp[1], config.apIp[2], config.apIp[3]);
    IPAddress apSn(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apGw, apSn);
  }

  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println("=== Setup Portal ===");
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("URL: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/scan", handleScan);
  server.on("/led", handleLed);
  server.on("/reset", handleReset);
  server.begin();
}

// ============================================================
// タリーモード起動
// ============================================================

void startTally() {
  portalMode = false;

  Serial.println("=== Tally Mode ===");
  Serial.printf("WiFi: %s\n", config.wifiSsid);
  Serial.printf("ATEM: %d.%d.%d.%d\n", config.atemIp[0], config.atemIp[1], config.atemIp[2], config.atemIp[3]);
  Serial.printf("Camera: %d\n", config.cameraNumber);

  WiFi.mode(WIFI_STA);

  // 固定IP設定
  if (config.useStaticIp == 1) {
    IPAddress ip(config.tallyIp[0], config.tallyIp[1], config.tallyIp[2], config.tallyIp[3]);
    IPAddress gw(config.gateway[0], config.gateway[1], config.gateway[2], config.gateway[3]);
    IPAddress sn(config.subnet[0], config.subnet[1], config.subnet[2], config.subnet[3]);
    WiFi.config(ip, gw, sn);
    Serial.printf("Static IP: %d.%d.%d.%d\n", config.tallyIp[0], config.tallyIp[1], config.tallyIp[2], config.tallyIp[3]);
  }

  WiFi.begin(config.wifiSsid, config.wifiPass);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    blinkState = !blinkState;
    setRGB(blinkState ? 128 : 0, 0, blinkState ? 128 : 0);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed! Starting portal...");
    startPortal();
    return;
  }

  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  IPAddress atemIP(config.atemIp[0], config.atemIp[1], config.atemIp[2], config.atemIp[3]);
  atemSwitcher.begin(atemIP);
  atemSwitcher.serialOutput(0x80);
  atemSwitcher.connect();

  currentState = TALLY_DISCONNECTED;
  Serial.println("ATEM connecting...");
}

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

    case TALLY_DISCONNECTED:
      if (millis() - lastBlinkTime > 500) {
        blinkState = !blinkState;
        setRGB(0, 0, blinkState ? 255 : 0);
        lastBlinkTime = millis();
      }
      break;

    case TALLY_PORTAL:
      if (millis() - lastBlinkTime > 800) {
        blinkState = !blinkState;
        uint8_t v = blinkState ? 255 : 0;
        setRGB(v, v, v);
        lastBlinkTime = millis();
      }
      break;
  }

  prevState = currentState;
}

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== ATEM Tally Light ===");

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  analogWriteRange(PWM_RANGE);
  setRGB(0, 0, 0);

  pinMode(RESET_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_PIN) == LOW) {
    Serial.println("Reset button pressed! Clearing config...");
    setRGB(255, 165, 0);
    clearConfig();
    delay(1000);
  }

  loadConfig();

  if (hasValidConfig()) {
    startTally();
  } else {
    Serial.println("No config found. Starting setup portal...");
    startPortal();
  }
}

// ============================================================
// Loop
// ============================================================

void loop() {
  if (portalMode) {
    server.handleClient();
    updateLED();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Restarting...");
    delay(1000);
    ESP.restart();
    return;
  }

  atemSwitcher.runLoop();

  if (atemSwitcher.isConnected()) {
    uint8_t tallyFlags = atemSwitcher.getTallyByIndexTallyFlags(config.cameraNumber - 1);
    bool program = tallyFlags & 0x01;
    bool preview = tallyFlags & 0x02;

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

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "reset") {
      Serial.println("Resetting config...");
      clearConfig();
      delay(500);
      ESP.restart();
    }
  }
}
