# ATEM Wireless Tally

ESP8266ベースのBlackmagic ATEM スイッチャー用ワイヤレスタリーライトシステム。
ブラウザから全ての設定が可能なキャプティブポータル付き。**WiFi版**と**LoRa版**の
2種類のファームウェアがあり、目的に合わせて選べます。

## 簡単インストール

書き込みだけなら PlatformIO 不要。ブラウザ（Chrome/Edge）から USB 経由で書き込めます。

https://kokitutaya0224.github.io/atem-wireless-tally/

## どちらを使う？

| | WiFi版 | LoRa版 |
|---|---|---|
| 通信 | タリー本体がATEMにWiFiで直接接続 | ベース局のみWiFi/ATEMに接続、受信機へ920MHz LoRaで配信 |
| WiFi環境 | 現場にWiFiが必要（各台がATEMと同じネットワークに入る） | ベース局のみWiFi必須。受信機はWiFi不要 |
| 台数構成 | 1台ずつ独立動作 | ベース局1台 + 受信機N台（1系統あたり最大20カメラ分） |
| 複数系統 | 台数分書き込むだけ | ベース局ごとに異なるLoRaチャンネル（CH0〜14）を設定すれば同一会場で複数系統を運用可能 |
| ビルド環境 | `pio run -e esp8266` | `pio run -e lora_base` / `pio run -e lora_tally` |
| 詳細 | 本README | [docs/lora-tally.md](docs/lora-tally.md)（配線・無線設定・トラブルシュートを別途記載） |

WiFiの届かない現場や、ATEMのネットワークにタリーを何台も参加させたくない場合はLoRa版が向いています。

## 機能

- ATEM スイッチャーとWiFi経由で直接通信
- ブラウザベースの設定ポータル（WiFiスキャン対応）
- カメラ 1〜20 選択
- 固定IP / DHCP 切り替え
- AP IPアドレスのカスタマイズ
- LEDテストボタン（PGM / PVW / OFF）
- 設定はEEPROMに保存（電源OFFでも保持）
- リセットボタンによる設定初期化

LoRa版の機能（ベース局・受信機共通の要点）や配線・無線設定は [docs/lora-tally.md](docs/lora-tally.md) を参照してください。

## LED表示（WiFi版）

| 色 | 状態 |
|---|---|
| 赤色（点灯） | PROGRAM（ON AIR） |
| 緑色（点灯） | PREVIEW |
| 消灯 | 非選択 |
| 青色（点滅） | ATEM未接続 |
| 白色（点滅） | 設定ポータルモード |

LoRa版のLED表示（電波ロスト表示などが追加）は [docs/lora-tally.md](docs/lora-tally.md#受信機の-led-表示) を参照。

## 必要なもの（WiFi版）

- ESP8266 開発ボード（NodeMCU v2 等）
- 4ピン RGB LED（コモンアノード）
- 220Ω抵抗 × 3
- ブレッドボードまたはユニバーサル基板

LoRa版の部品リスト（E220モジュール・アンテナ等）は [docs/lora-tally.md](docs/lora-tally.md#使用モジュール) を参照。

## 配線（WiFi版）

```
RGB LED (コモンアノード)
├── R（赤）      → D5 (GPIO14)  ※220Ω抵抗経由
├── 長い足(VCC)  → 3.3V
├── G（緑）      → D6 (GPIO12)  ※220Ω抵抗経由
└── B（青）      → D7 (GPIO13)  ※220Ω抵抗経由

リセットボタン（オプション）
└── D1 (GPIO5) → GND
```

## セットアップ（WiFi版）

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
