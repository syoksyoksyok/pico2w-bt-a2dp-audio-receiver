# Pico 2 W Bluetooth A2DP Audio Receiver

Raspberry Pi Pico 2 W を Bluetooth オーディオレシーバー（A2DP Sink: Bluetooth 音声の受信・再生側）として動作させ、スマホから受信した音声を PCM5102A I2S DAC へ出力するプログラムです。

## できること

- iPhone / Android から Bluetooth スピーカーとして接続
- Bluetooth A2DP の SBC 音声を 16bit stereo PCM にデコード
- PIO + DMA で PCM5102A へ I2S 出力
- 再生中に別スマホから接続して音声接続を奪い取り可能
- USB シリアルログで接続状態を確認可能

## 必要なもの

| 種類 | 内容 |
|---|---|
| MCU | Raspberry Pi Pico 2 W |
| DAC | PCM5102A I2S DAC モジュール |
| 出力先 | ライン入力付きアンプ、アクティブスピーカー、またはヘッドホンアンプ |
| その他 | USB ケーブル、ジャンパーワイヤー、必要に応じてブレッドボード |

PCM5102A はスピーカーを直接駆動するアンプではありません。パッシブスピーカーを使う場合は、PCM5102A の後段にアンプを接続してください。

## 重要な前提

このプロジェクトの配線は、PCM5102A モジュールが [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png) と同じジャンパ設定になっていることを前提にしています。

ジャンパ設定が異なる場合、Bluetooth 接続はできても音が出ないことがあります。配線前に [WIRING.md](WIRING.md) を確認してください。

## 配線概要

デフォルトの I2S ピンは以下です。

| Pico 2 W | PCM5102A | 用途 |
|---|---|---|
| GPIO 26 | DIN | I2S データ |
| GPIO 27 | BCK / BCLK | I2S ビットクロック |
| GPIO 28 | LCK / LRCK / WS | I2S 左右チャンネルクロック |
| 3V3 | VCC / VIN | DAC 電源 |
| GND | GND | 共通 GND |

詳しい配線、PCM5102A のジャンパ設定、音が出ない場合の確認項目は [WIRING.md](WIRING.md) を参照してください。

## ビルド環境

- Pico SDK 2.x
- CMake 3.13 以降
- Arm GNU Toolchain
- Git

`PICO_SDK_PATH` を Pico SDK の場所に設定してからビルドします。

```bash
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$PWD
```

Windows の PowerShell では例として以下のように設定します。

```powershell
$env:PICO_SDK_PATH = "C:\pico\pico-sdk"
```

## ビルド方法

```bash
git clone https://github.com/syoksyoksyok/pico2w-bt-a2dp-audio-receiver.git
cd pico2w-bt-a2dp-audio-receiver
mkdir build
cd build
cmake ..
cmake --build .
```

成功すると以下が生成されます。

```text
build/pico2w_bt_a2dp_receiver.uf2
```

## Pico 2 W への書き込み

1. Pico 2 W の `BOOTSEL` ボタンを押しながら USB 接続する
2. PC に `RPI-RP2` ドライブとして表示される
3. 生成された `pico2w_bt_a2dp_receiver.uf2` を `RPI-RP2` へコピーする
4. Pico 2 W が自動再起動する

## 使い方

1. Pico 2 W を起動する
2. スマホの Bluetooth 設定を開く
3. `Pico2W Audio Receiver` を選んで接続する
4. スマホの音楽アプリで再生する

USB シリアルログを確認する場合は 115200 bps で開きます。

```bash
screen /dev/ttyACM0 115200
```

起動後、接続できる状態になると以下のような案内が出ます。

```text
Ready! Waiting for Bluetooth connection...
Device name: Pico2W Audio Receiver
```

## 設定変更

主な設定は [src/config.h](src/config.h) にあります。

| 設定 | 初期値 | 内容 |
|---|---:|---|
| `BT_DEVICE_NAME` | `Pico2W Audio Receiver` | Bluetooth 表示名 |
| `AUDIO_SAMPLE_RATE` | `44100` | サンプリングレート |
| `I2S_DATA_PIN` | `26` | I2S DATA |
| `I2S_BCLK_PIN` | `27` | I2S BCLK |
| `I2S_LRCLK_PIN` | `28` | I2S LRCLK |
| `AUDIO_BUFFER_SIZE` | 1 秒分 | 受信揺らぎ吸収用リングバッファ |
| `AUTO_START_THRESHOLD` | 約 33% | 再生開始までに貯めるバッファ量 |

現在の A2DP / I2S 設定は 44.1kHz 前提です。48kHz へ変更する場合は、`config.h` だけでなく `src/bt_audio.c` の SBC capability と I2S 関連計算も合わせて変更してください。

## トラブルシューティング

### スマホから見えない

- Pico 2 W が起動しているか確認する
- スマホ側 Bluetooth をオフ/オンする
- Pico 2 W を再起動する

### 接続できるが音が出ない

- [WIRING.md](WIRING.md) の配線を確認する
- PCM5102A のジャンパ設定が [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png) と同じか確認する
- Pico 2 W と PCM5102A の GND が共通か確認する
- スマホ側、アンプ側、スピーカー側の音量を確認する
- Android では一度ペアリング情報を削除して再接続する

### 音が途切れる、ノイズが入る

- USB ハブではなく PC 直結、または安定した USB 電源を使う
- I2S 配線を短くする
- GND 配線を短く確実にする
- PCM5102A の電源近くに 0.1uF 程度のコンデンサを追加する

## ファイル構成

| パス | 内容 |
|---|---|
| `README.md` | 概要、ビルド、使い方 |
| `WIRING.md` | Pico 2 W と PCM5102A の配線 |
| `PCM5102A_jumper connection.png` | 前提となる PCM5102A ジャンパ設定の写真 |
| `src/main.c` | メインループと初期化 |
| `src/bt_audio.c` | Bluetooth A2DP Sink 処理 |
| `src/audio_out_i2s.c` | I2S / DMA 音声出力 |
| `src/config.h` | ユーザー設定 |
| `src/i2s.pio` | I2S 出力用 PIO プログラム |
| `CMakeLists.txt` | ビルド設定 |

## 技術メモ

```text
スマホ
  -> Bluetooth A2DP SBC
  -> BTstack A2DP Sink
  -> SBC Decoder
  -> 16bit stereo PCM ring buffer
  -> DMA ping-pong buffer
  -> PIO I2S output
  -> PCM5102A I2S DAC
  -> アンプ / スピーカー
```

主な現在値:

- Bluetooth: Classic A2DP Sink
- SBC: 44.1kHz, Stereo / Joint Stereo, bitpool max 35
- PCM: 16bit stereo
- I2S BCLK: 1.4112 MHz at 44.1kHz / 16bit / stereo
- I2S PIO: 64 cycles / stereo sample
- Ring buffer: 44100 stereo samples
- DMA buffer: 512 stereo samples x 2
- A2DP signaling connection slots: 2（接続奪い取り用）

## ライセンス

MIT License. 詳細は [LICENSE](LICENSE) を参照してください。

## 参考資料

- [Raspberry Pi Pico SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)
- [BTstack Documentation](https://bluekitchen-gmbh.com/btstack/)
- [PCM5102A データシート](https://www.ti.com/product/PCM5102A)
