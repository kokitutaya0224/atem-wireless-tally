# ATEM Wireless Tally

ESP8266ベースのBlackmagic ATEM スイッチャー用ワイヤレスタリーライトシステム。
ブラウザから全ての設定が可能なキャプティブポータル付き。

## 機能

- ATEM スイッチャーとWiFi経由で直接通信
- ブラウザベースの設定ポータル（WiFiスキャン対応）
- カメラ 1〜10 選択
- 固定IP / DHCP 切り替え
- AP IPアドレスのカスタマイズ
- LEDテストボタン（PGM / PVW / OFF）
- 設定はEEPROMに保存（電源OFFでも保持）
- リセットボタンによる設定初期化

## LED表示

| 色 | 状態 |
|---|---|
| 赤色（点灯） | PROGRAM（ON AIR） |
| 緑色（点灯） | PREVIEW |
| 消灯 | 非選択 |
| 青色（点滅） | ATEM未接続 |
| 白色（点滅） | 設定ポータルモード |

## 必要なもの

- ESP8266 開発ボード（NodeMCU v2 等）
- 4ピン RGB LED（コモンアノード）
- 220Ω抵抗 × 3
- ブレッドボードまたはユニバーサル基板

## 配線

```
RGB LED (コモンアノード)
├── R（赤）      → D5 (GPIO14)  ※220Ω抵抗経由
├── 長い足(VCC)  → 3.3V
├── G（緑）      → D6 (GPIO12)  ※220Ω抵抗経由
└── B（青）      → D7 (GPIO13)  ※220Ω抵抗経由

リセットボタン（オプション）
└── D1 (GPIO5) → GND
```

## セットアップ

### 1. ビルド & 書き込み

[PlatformIO](https://platformio.org/) を使用します。

```bash
# ビルド
pio run

# 書き込み
pio run --target upload
```

`platformio.ini` の `upload_port` は環境に合わせて変更してください。

### 2. 初回設定

1. ESP8266が起動すると **ATEM-Tally** という WiFi AP が立ち上がります
2. スマホまたはPCで接続
3. ブラウザで `http://192.168.4.1` にアクセス
4. WiFiネットワーク、ATEM IPアドレス、カメラ番号を設定
5. 「Save & Restart」で保存 → 自動的にタリーモードへ

### 3. 設定のリセット

- **ボタン**: D1ピンのリセットボタンを押しながら電源ON
- **シリアル**: `reset` と送信（115200bps）
- **ブラウザ**: `http://<IP>/reset` にアクセス

## ライブラリ

- [SKAARHOJ ATEMmin](https://github.com/kasperskaarhoj/SKAARHOJ-Open-Engineering) - ATEM通信プロトコル（`lib/` に同梱）

## ライセンス

SKAARHOJ ライブラリは GPL v3 に基づきます。詳細は `lib/ATEMmin/license.txt` を参照してください。
