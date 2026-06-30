# Pico 2 W Bluetooth A2DP Audio Receiver

Raspberry Pi Pico 2 W を Bluetooth オーディオレシーバー（A2DP Sink）として動作させ、スマホから受信した音声を I2S DAC へ出力するプログラムです。

## 概要

このプロジェクトは Raspberry Pi Pico 2 W（RP2350 + CYW43）専用です。Bluetooth Classic の A2DP Sink としてスマホから SBC 音声を受信し、BTstack の SBC デコーダーで PCM に変換して、PIO + DMA で I2S DAC に送ります。

### 主な機能

- **A2DP Sink プロファイル対応**: iPhone/Android から Bluetooth スピーカーとして認識される
- **SBC コーデックのデコード**: Bluetooth 音声ストリームを 16bit stereo PCM に変換
- **I2S DAC 出力専用**: PCM5102A, UDA1334A, MAX98357A などの I2S DAC/アンプに対応
- **PIO + DMA + リングバッファ方式**: Bluetooth 処理を優先しながら連続再生する
- **USB シリアルログ**: 接続状態、バッファ状態、underrun/overrun を確認できる

## 必要なもの

### ハードウェア

1. Raspberry Pi Pico 2 W x 1
2. I2S DAC モジュール x 1
   - 推奨: PCM5102A
   - 利用可: UDA1334A, MAX98357A など
3. スピーカー、アンプ、またはヘッドホン出力に接続できるオーディオ機器
4. Micro USB ケーブル
5. ブレッドボードとジャンパーワイヤー

### BOM（部品表）

PCM5102A を使う標準構成の部品表です。MAX98357A を使う場合は、I2S DAC モジュールを MAX98357A モジュールに置き換えてください。

| No. | 部品 | 数量 | 用途 / 備考 |
|-----|------|------|-------------|
| 1 | Raspberry Pi Pico 2 W | 1 | MCU。Bluetooth A2DP Sink と I2S 出力を担当 |
| 2 | PCM5102A I2S DAC モジュール | 1 | I2S をアナログ L/R 音声に変換。`DIN`, `BCK`, `LCK/LRCK`, `VCC`, `GND` があるもの |
| 3 | アンプまたはアクティブスピーカー | 1 | PCM5102A の `OUTL` / `OUTR` を接続。ヘッドホン直結よりアンプ経由を推奨 |
| 4 | スピーカーまたはヘッドホン | 1 | 使用するアンプ/DAC 出力に合わせる |
| 5 | Micro USB ケーブル | 1 | Pico 2 W への給電、書き込み、USB シリアルログ確認 |
| 6 | ブレッドボード | 1 | 試作配線用。直配線する場合は省略可 |
| 7 | ジャンパーワイヤー | 10本程度 | I2S 信号、電源、GND、PCM5102A 設定ピンの接続 |
| 8 | 0.1uF セラミックコンデンサ | 1以上 | 任意。DAC 電源近くのデカップリング用。ノイズ対策 |

PCM5102A の代わりに MAX98357A を使う場合の差分です。

| 置き換える部品 | 数量 | 備考 |
|----------------|------|------|
| MAX98357A I2S アンプモジュール | 1 | `DIN`, `BCLK`, `LRC/LRCLK`, `VIN`, `GND`, スピーカー出力 `+` / `-` を使用 |
| 外部電源 | 必要に応じて | 大きめのスピーカーを鳴らす場合に使用。GND は Pico 2 W と共通化する |

### ソフトウェア

1. Pico 2 W / RP2350 に対応した Pico SDK 2.x
2. CMake 3.13 以降
3. GNU Arm Embedded Toolchain
4. Git

## セットアップ

### 1. Pico SDK の準備

Pico SDK をインストールし、`PICO_SDK_PATH` を設定します。

```bash
cd ~
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=~/pico-sdk
```

### 2. プロジェクトの取得

```bash
git clone https://github.com/syoksyoksyok/pico2w-bt-a2dp-audio-receiver.git
cd pico2w-bt-a2dp-audio-receiver
```

### 3. ピン設定の確認

デフォルトの I2S ピンは `src/config.h` で定義されています。

```c
#define I2S_DATA_PIN    26    // DAC DIN
#define I2S_BCLK_PIN    27    // DAC BCK
#define I2S_LRCLK_PIN   28    // DAC LCK / LRCK / WS
```

配線の詳細は [WIRING.md](WIRING.md) を参照してください。

### 4. ビルド

`.uf2` を生成する通常ビルドです。Pico SDK が `picotool` を見つけられない場合は、SDK がビルド用の picotool を準備します。

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

成功すると `build/pico2w_bt_a2dp_receiver.uf2` が生成されます。

`-DPICO_NO_PICOTOOL=1` を付けると picotool を使わないため、環境によっては `.elf`、`.bin`、`.hex` だけが生成され、`.uf2` は生成されません。

### 5. Pico 2 W への書き込み

1. Pico 2 W の BOOTSEL ボタンを押しながら USB 接続する
2. PC に `RPI-RP2` ドライブとしてマウントされる
3. `pico2w_bt_a2dp_receiver.uf2` を `RPI-RP2` ドライブへコピーする
4. Pico 2 W が自動再起動してプログラムが開始される

## 使い方

### 起動ログの確認

USB シリアルを 115200 bps で開きます。

```bash
screen /dev/ttyACM0 115200
```

起動時の主なログ例です。

```text
================================================
  Pico 2W Bluetooth A2DP Audio Receiver
================================================

Configuration:
  Device name: Pico2W Audio Receiver
  Output mode: I2S DAC
  I2S pins: DATA=26, BCLK=27, LRCLK=28
  Sample rate: 44100 Hz
  Channels: 2 (Stereo)
  Buffer size: 44100 samples
```

### スマホから接続

1. スマホの Bluetooth 設定を開く
2. `Pico2W Audio Receiver` を選択する
3. ペアリング後、音楽アプリから再生する

接続時のログ例です。

```text
A2DP connection established: XX:XX:XX:XX:XX:XX (CID: 0x0040)
Stream established: XX:XX:XX:XX:XX:XX
SBC configuration received: channels 2, sample rate 44100 Hz
Stream started - Audio playback begins

>>> Audio stream connected!
```

## 設定

### デバイス名

`src/config.h` で変更できます。

```c
#define BT_DEVICE_NAME "Pico2W Audio Receiver"
```

### サンプリングレート

現在の A2DP SBC capability と I2S 設定は **44.1kHz 固定**です。

```c
#define AUDIO_SAMPLE_RATE 44100
```

48kHz 化する場合は、`AUDIO_SAMPLE_RATE` だけでなく `bt_audio.c` の SBC capability と I2S 初期化、バッファ計算を一貫して変更してください。

### バッファサイズ

```c
#define AUDIO_BUFFER_SIZE (AUDIO_SAMPLE_RATE * 1)  // 1秒分 = 44100ステレオペア
#define DMA_BUFFER_SIZE   512                      // 約11.6ms分
```

メモリ使用量の目安です。

- 1秒分: 約176KB（16bit stereo のリングバッファとして使用。推奨）
- 2秒分: 約352KB（RP2350 の SRAM 264KB を超えるため不可）
- 4秒分: 約705KB（不可）

## トラブルシューティング

### スマホから見えない

- Pico 2 W が起動しているか USB シリアルログで確認する
- Pico 2 W の電源を入れ直す
- スマホ側 Bluetooth をオフ/オンする

### 接続できるが音が出ない

- [WIRING.md](WIRING.md) の I2S 配線を確認する
- Pico 2 W と DAC の GND が共通になっているか確認する
- PCM5102A の `XSMT` が 3.3V、`FMT` が GND、`SCK` が GND になっているか確認する
- スマホ側とアンプ側の音量を確認する

### 音が途切れる、ノイズが入る

- USB シリアルログで `Underruns`、`Overruns`、`Dropped` を確認する
- USB ハブ経由なら PC 直結または安定した電源に変更する
- I2S の配線を短くし、GND 接続を確実にする
- DAC の電源近くに 0.1uF 程度のデカップリングコンデンサを追加する

## 技術詳細

### データ経路

```text
iPhone/Android
  -> Bluetooth A2DP SBC
  -> BTstack A2DP Sink
  -> SBC Decoder
  -> 16bit stereo PCM ring buffer
  -> DMA ping-pong buffer
  -> PIO I2S output
  -> I2S DAC / amplifier
```

### 現在の主要設定

- Bluetooth: Classic A2DP Sink
- SBC: 44.1kHz stereo
- PCM: 16bit stereo
- I2S PIO: 66 cycles / stereo sample
- Ring buffer: 44100 stereo samples（1秒分）
- DMA buffer: 512 stereo samples x 2
- Auto start threshold: 20%（8820 stereo samples）
- DMA IRQ priority: 0xFF（Bluetooth 処理を優先）

## ライセンス

MIT License

## 参考資料

- [Raspberry Pi Pico SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)
- [BTstack Documentation](https://bluekitchen-gmbh.com/btstack/)
- [A2DP Profile Specification](https://www.bluetooth.com/specifications/specs/advanced-audio-distribution-profile-1-3/)
