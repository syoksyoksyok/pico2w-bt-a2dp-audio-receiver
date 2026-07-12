# 配線ガイド

このプロジェクトは Raspberry Pi Pico 2 W から PCM5102A I2S DAC へ音声を出力します。PWM 出力や I2S アンプ直結構成は、この配線ガイドの対象外です。

## 前提構成

- MCU: Raspberry Pi Pico 2 W
- DAC: PCM5102A I2S DAC モジュール（[PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png) と同じジャンパ設定済み）
- 出力先: ライン入力付きアンプ、アクティブスピーカー、またはヘッドホンアンプ
- 音声信号: 44.1kHz / 16bit / stereo PCM を I2S で出力

PCM5102A はスピーカーを直接駆動するアンプではありません。`OUTL` / `OUTR` はラインレベル出力として扱い、パッシブスピーカーを鳴らす場合は別途アンプを接続してください。

**重要:** この配線ガイドは、[PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png) と同じジャンパ設定になっている PCM5102A モジュールを前提にしています。ジャンパ未設定、または設定が異なるモジュールでは、この配線だけでは無音になる場合があります。

## I2S と I2C の違い

このプロジェクトでは PCM5102A を I2S DAC として使います。I2C の `SDA` / `SCL` は使いません。

| 種類 | 信号名 | このプロジェクトでの扱い |
|---|---|---|
| I2S | `DIN`, `BCK` / `BCLK`, `LCK` / `LRCK` / `WS` | 使用する |
| I2C | `SDA`, `SCL` | 使用しない |

PCM5102A モジュール側の端子表記が `DATA`、`SD`、`D` のようになっている場合は `DIN` 相当の可能性があります。`BCK` / `BCLK` と `LCK` / `LRCK` / `WS` が見つからない場合は、基板の端子表記や商品ページを確認してください。

## デフォルトの I2S ピン

`src/config.h` の初期値です。

```c
#define I2S_DATA_PIN    26
#define I2S_BCLK_PIN    27
#define I2S_LRCLK_PIN   28
```

## Pico 2 W と PCM5102A の接続

### I2S 信号と電源

以下の接続は、PCM5102A 側が [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png) と同じジャンパ設定済みであることを前提にしています。

| Pico 2 W | PCM5102A | 用途 |
|---|---|---|
| `GPIO 26` | `DIN` | I2S データ |
| `GPIO 27` | `BCK` / `BCLK` | I2S ビットクロック |
| `GPIO 28` | `LCK` / `LRCK` / `WS` | I2S 左右チャンネルクロック |
| `3V3` | `VCC` / `VIN` | DAC 電源 |
| `GND` | `GND` | 共通 GND |

### PCM5102A のジャンパ設定

この配線ガイドでは、[PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png) のように PCM5102A モジュール側の設定ジャンパが済んでいることを必須前提とします。Pico 2 W から `XSMT`、`FMT`、`FLT`、`DEMP` へ個別にジャンパ線を引く必要はありません。

ジャンパ設定は以下の状態を前提にします。

| 設定 | 状態 | 目的 |
|---|---|---|
| `XSMT` | `H` / High | ミュート解除 |
| `FMT` | I2S | I2S フォーマット選択 |
| `FLT` | Normal / Low | ノーマルフィルター |
| `DEMP` | Off / Low | De-emphasis 無効 |
| `SCK` | 未使用または `GND` | 3-wire I2S / 内部 PLL 動作 |

`SCK` ピンが独立して出ているモジュールでは、音が出ない場合に `SCK` を `GND` へ接続してください。画像と同じジャンパ済みモジュールで正常に動作する場合は、追加配線は不要です。

### PCM5102A の音声出力

| PCM5102A | 接続先 |
|---|---|
| `OUTL` | アンプ/ライン入力の Left |
| `OUTR` | アンプ/ライン入力の Right |
| `AGND` / `GND` | アンプ/ライン入力の GND |

ヘッドホンを直接接続するより、アンプまたはヘッドホンアンプを通す構成を推奨します。

## Pico 2 W のピンアサイン（物理ピン目安）

Pico 2 W を USB コネクタが上になる向きで見た場合の代表的な接続先です。`GP26` は `GPIO 26` と同じピンを指します。

| 物理ピン | Pico 2 W | PCM5102A |
|---|---|---|
| `31` | `GP26` | `DIN` |
| `32` | `GP27` | `BCK` / `BCLK` |
| `34` | `GP28` | `LCK` / `LRCK` / `WS` |
| `36` | `3V3` | `VCC` / `VIN` |
| `38` | `GND` | `GND` |

`GP28` は物理ピン `34` です。物理ピン `35` は `ADC_VREF` なので、`LCK` / `LRCK` / `WS` には接続しないでください。

## 配線例

```text
Raspberry Pi Pico 2 W          PCM5102A

GPIO 26 ---------------------> DIN
GPIO 27 ---------------------> BCK / BCLK
GPIO 28 ---------------------> LCK / LRCK / WS
3V3     ---------------------> VCC / VIN
GND     ---------------------> GND

OUTL    ---------------------> Amplifier / line input Left
OUTR    ---------------------> Amplifier / line input Right
AGND    ---------------------> Amplifier / line input GND
```

## 電源と GND の注意

- Pico 2 W と PCM5102A の GND は必ず共通接続します。
- PCM5102A は Pico 2 W の `3V3` から給電できます。
- PCM5102A の電源ピン近くに 0.1uF 程度のセラミックコンデンサを置くと、ノイズ低減に効く場合があります。
- アンプ側に別電源を使う場合も、音声 GND は Pico 2 W / PCM5102A 側と適切に共通化してください。
- I2S 配線はできるだけ短くし、GND 配線も短く確実に接続してください。

## ピンを変更する場合

`src/config.h` を変更します。`I2S_BCLK_PIN` と `I2S_LRCLK_PIN` は PIO の side-set で連続ピンとして扱うため、LRCLK は BCLK の次の GPIO にしてください。

```c
#define I2S_DATA_PIN    20
#define I2S_BCLK_PIN    21
#define I2S_LRCLK_PIN   22
```

この例では以下の接続になります。

| Pico 2 W | PCM5102A |
|---|---|
| `GPIO 20` | `DIN` |
| `GPIO 21` | `BCK` / `BCLK` |
| `GPIO 22` | `LCK` / `LRCK` / `WS` |

## 音が出ない場合の確認

- `GPIO 26`、`GPIO 27`、`GPIO 28` が PCM5102A の `DIN`、`BCK`、`LCK/LRCK` に対応しているか確認する。
- Pico 2 W と PCM5102A の GND が共通か確認する。
- PCM5102A のジャンパ設定が [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png) と同じになっているか確認する。
- `SCK` ピンが独立しているモジュールで音が出ない場合は、`SCK` を `GND` に接続して確認する。
- PCM5102A の `OUTL` / `OUTR` がアンプやライン入力へ接続されているか確認する。
- スマホ側、アンプ側、スピーカー側の音量を確認する。
- USB シリアルログで Bluetooth 接続と `Underruns` / `Overruns` を確認する。

## ノイズが多い場合の確認

- I2S 配線を短くする。
- GND 配線を短く確実にする。
- PCM5102A の電源近くに 0.1uF コンデンサを追加する。
- USB ハブではなく PC 直結、または安定した USB 電源を使う。
- アンプの電源 GND と PCM5102A の音声 GND の取り回しを見直す。

## 参考資料

- [PCM5102A データシート](https://www.ti.com/product/PCM5102A)
- [Raspberry Pi Pico Pinout](https://datasheets.raspberrypi.com/pico/Pico-R3-A4-Pinout.pdf)
- [I2S 規格解説](https://en.wikipedia.org/wiki/I%C2%B2S)
