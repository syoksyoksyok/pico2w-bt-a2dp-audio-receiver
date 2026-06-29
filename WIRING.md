# 配線ガイド

このプロジェクトは Raspberry Pi Pico 2 W から I2S DAC へ音声を出力します。PWM 出力は現在のビルド対象ではありません。

## 対応する構成

- MCU: Raspberry Pi Pico 2 W
- 出力: I2S DAC / I2S アンプ
- 推奨 DAC: PCM5102A
- 利用可: UDA1334A, MAX98357A など

## デフォルトの I2S ピン

`src/config.h` の初期値です。

```c
#define I2S_DATA_PIN    26
#define I2S_BCLK_PIN    27
#define I2S_LRCLK_PIN   28
```

## Pico 2 W と PCM5102A の接続

### MCU ピンと DAC 入力

- Pico 2 W `GPIO 26` -> PCM5102A `DIN`
- Pico 2 W `GPIO 27` -> PCM5102A `BCK` / `BCLK`
- Pico 2 W `GPIO 28` -> PCM5102A `LCK` / `LRCK` / `WS`
- Pico 2 W `3V3` -> PCM5102A `VCC` / `VIN`
- Pico 2 W `GND` -> PCM5102A `GND`

### PCM5102A の設定ピン

- PCM5102A `XSMT` -> `3V3`
  - ミュート解除です。未接続だと無音になるモジュールがあります。
- PCM5102A `FMT` -> `GND`
  - I2S フォーマット選択です。
- PCM5102A `SCK` -> `GND`
  - PCM5102A を 3-wire I2S / 内部 PLL 動作にします。モジュールによっては SCK 未処理だと無音になります。
- PCM5102A `FLT` -> `GND`
  - ノーマルフィルターです。
- PCM5102A `DEMP` -> `GND`
  - De-emphasis 無効です。

### PCM5102A の音声出力

- PCM5102A `OUTL` -> アンプ/ヘッドホン入力の Left
- PCM5102A `OUTR` -> アンプ/ヘッドホン入力の Right
- PCM5102A `AGND` / `GND` -> アンプ/ヘッドホン入力の GND

## Pico 2 W の物理ピン目安

Pico 2 W を USB コネクタが上になる向きで見た場合の代表的な接続先です。

- 物理ピン `31` / `GP26` -> PCM5102A `DIN`
- 物理ピン `34` / `GP27` -> PCM5102A `BCK`
- 物理ピン `35` / `GP28` -> PCM5102A `LCK` / `LRCK`
- 物理ピン `36` / `3V3` -> PCM5102A `VCC` / `VIN`
- 物理ピン `38` / `GND` -> PCM5102A `GND`

## 配線例

```text
Raspberry Pi Pico 2 W          PCM5102A

GPIO 26 ---------------------> DIN
GPIO 27 ---------------------> BCK
GPIO 28 ---------------------> LCK / LRCK
3V3     ---------------------> VCC / VIN
GND     ---------------------> GND

3V3     ---------------------> XSMT
GND     ---------------------> FMT
GND     ---------------------> SCK
GND     ---------------------> FLT
GND     ---------------------> DEMP

OUTL    ---------------------> Left audio input
OUTR    ---------------------> Right audio input
AGND    ---------------------> Audio GND
```

## MAX98357A を使う場合

MAX98357A は I2S 入力付きのモノラルアンプです。端子名が違う場合があります。

- Pico 2 W `GPIO 26` -> MAX98357A `DIN`
- Pico 2 W `GPIO 27` -> MAX98357A `BCLK`
- Pico 2 W `GPIO 28` -> MAX98357A `LRC` / `LRCLK`
- Pico 2 W `3V3` または外部電源 -> MAX98357A `VIN`
- Pico 2 W `GND` -> MAX98357A `GND`
- MAX98357A `+` / `-` -> スピーカー

MAX98357A はスピーカーを直接駆動できます。消費電流が大きい場合は、Pico 2 W の 3V3 からではなく、モジュール仕様に合う外部電源を使ってください。その場合も GND は Pico 2 W と共通にします。

## 電源と GND の注意

- Pico 2 W と DAC/アンプの GND は必ず共通接続します。
- PCM5102A のような小型 DAC は Pico 2 W の 3V3 から給電できます。
- アンプ内蔵モジュールや大きなスピーカーを鳴らす構成では、別電源を検討してください。
- DAC の電源ピン近くに 0.1uF 程度のデカップリングコンデンサを置くとノイズ低減に効く場合があります。

## ピンを変更する場合

`src/config.h` を変更します。`I2S_BCLK_PIN` と `I2S_LRCLK_PIN` は PIO の side-set で連続ピンとして扱うため、LRCLK は BCLK の次の GPIO にしてください。

```c
#define I2S_DATA_PIN    20
#define I2S_BCLK_PIN    21
#define I2S_LRCLK_PIN   22
```

この例では以下の接続になります。

- Pico 2 W `GPIO 20` -> DAC `DIN`
- Pico 2 W `GPIO 21` -> DAC `BCK`
- Pico 2 W `GPIO 22` -> DAC `LCK` / `LRCK`

## 音が出ない場合の確認

- `GPIO 26`、`GPIO 27`、`GPIO 28` が DAC の `DIN`、`BCK`、`LCK/LRCK` に対応しているか確認する
- Pico 2 W と DAC の GND が共通か確認する
- PCM5102A の `XSMT` が 3V3 に接続されているか確認する
- PCM5102A の `FMT` と `SCK` が GND に接続されているか確認する
- DAC の出力先アンプ、スピーカー、音量を確認する
- USB シリアルログで Bluetooth 接続と `Underruns` / `Overruns` を確認する

## ノイズが多い場合の確認

- I2S 配線を短くする
- GND 配線を確実にする
- DAC の電源近くに 0.1uF コンデンサを追加する
- USB ハブではなく PC 直結、または安定した電源を使う
- アンプやスピーカーの電源を DAC/MCU の信号 GND と適切に共通化する

## 参考資料

- [PCM5102A データシート](https://www.ti.com/product/PCM5102A)
- [Raspberry Pi Pico Pinout](https://datasheets.raspberrypi.com/pico/Pico-R3-A4-Pinout.pdf)
- [I2S 規格解説](https://en.wikipedia.org/wiki/I%C2%B2S)
