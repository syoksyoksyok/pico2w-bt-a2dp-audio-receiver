# Pico 2W Bluetooth A2DP Audio Receiver

Raspberry Pi Pico 2 W を Bluetooth オーディオレシーバー（A2DP Sink）として動作させ、iPhone/Android スマホから音声を受信して再生するプログラムです。

## 概要

このプロジェクトは、Raspberry Pi Pico 2 W（RP2350）を使用して、Bluetooth A2DP（Advanced Audio Distribution Profile）対応のオーディオレシーバーを実現します。

### 主な機能

- **A2DP Sink プロファイル対応**: iPhone/Android から Bluetooth スピーカーとして認識される
- **SBC コーデックのデコード**: Bluetooth 音声ストリームを PCM データに変換
- **2つの出力モード**:
  - **I2S DAC 出力**: PCM5102A などの高音質 DAC に対応
  - **PWM 簡易 DAC 出力**: ヘッドホン直接接続などの簡易用途向け
- **DMA + リングバッファ方式**: 音切れしにくい安定した再生
- **詳細なログ出力**: USB シリアルでデバッグ情報を確認可能

## 必要なもの

### ハードウェア

1. **Raspberry Pi Pico 2 W** × 1
2. **オーディオ出力デバイス**（以下のいずれか）:
   - **I2S DAC モジュール**: PCM5102A, UDA1334A など
   - **スピーカー/ヘッドホン**: PWM モード用（ローパスフィルタ推奨）
3. **Micro USB ケーブル**: プログラム書き込みと電源供給用
4. **ブレッドボードとジャンパーワイヤー**: 配線用

### ソフトウェア

1. **Pico SDK**: v1.5.0 以降
2. **CMake**: v3.13 以降
3. **GNU Arm Embedded Toolchain**: arm-none-eabi-gcc
4. **Git**: ソースコード取得用

## セットアップ

### 1. Pico SDK のインストール

Pico SDK がまだインストールされていない場合は、以下の手順でインストールします。

```bash
# Pico SDK のクローン
cd ~
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init

# 環境変数を設定（~/.bashrc または ~/.zshrc に追加）
export PICO_SDK_PATH=~/pico-sdk
```

### 2. プロジェクトのクローン

```bash
git clone https://github.com/YOUR_USERNAME/pico2w-bt-a2dp-audio-receiver.git
cd pico2w-bt-a2dp-audio-receiver
```

### 3. 出力モードの選択

`src/config.h` を編集して、出力モードを選択します。

```c
// I2S DAC 出力を使用する場合
#define USE_I2S_OUTPUT     1

// PWM 簡易 DAC 出力を使用する場合（上記をコメントアウトして以下を有効化）
// #define USE_PWM_OUTPUT     1
```

### 4. ピン配置の確認・変更

`src/config.h` でピン配置を確認・変更できます。

#### I2S モードの場合

```c
#define I2S_DATA_PIN    26    // DIN (Data)
#define I2S_BCLK_PIN    27    // BCK (Bit Clock)
#define I2S_LRCLK_PIN   28    // LCK (Left/Right Clock)
```

#### PWM モードの場合

```c
#define PWM_AUDIO_PIN   26    // PWM 出力ピン
```

詳細な配線方法は [WIRING.md](WIRING.md) を参照してください。

### 5. ビルド

**重要**: ビルド前に Pico SDK のパスを環境変数に設定してください。

```bash
export PICO_SDK_PATH=~/pico/pico-sdk  # Pico SDK のパスを指定
cd ~/pico2w-bt-a2dp-audio-receiver
rm -rf build
mkdir build
cd build
cmake -DPICO_NO_PICOTOOL=1 ..
make -j4
```

ビルドが成功すると、`build/pico2w_bt_a2dp_receiver.uf2` ファイルが生成されます。

**注意**:
- `PICO_SDK_PATH` は実際の Pico SDK のインストール先に合わせて変更してください
- `-DPICO_NO_PICOTOOL=1` フラグは picotool のビルドをスキップします（mbedtls エラー回避）
- `-j4` は並列ビルドでビルド時間を短縮します

### 6. Pico 2 W への書き込み

1. Pico 2 W の **BOOTSEL ボタンを押しながら** USB ケーブルを接続
2. PC に `RPI-RP2` ドライブとしてマウントされる
3. `pico2w_bt_a2dp_receiver.uf2` を `RPI-RP2` ドライブにドラッグ＆ドロップ
4. 自動的に再起動し、プログラムが実行される

## 使い方

### 1. Pico 2 W の起動確認

USB シリアルターミナル（例: screen, minicom, PuTTY など）を開いて、ログを確認します。

```bash
# Linux/Mac の場合
screen /dev/ttyACM0 115200

# または
minicom -D /dev/ttyACM0 -b 115200
```

起動すると、以下のようなログが表示されます：

```
================================================
  Pico 2W Bluetooth A2DP Audio Receiver
================================================

Configuration:
  Device name: Pico2W Audio Receiver
  Output mode: I2S DAC
  I2S pins: DATA=26, BCLK=27, LRCLK=28
  Sample rate: 44100 Hz
  Channels: 2 (Stereo)
  Ring buffer: 44100 samples (1.0 sec)
  DMA buffer: 512 samples (11.6 ms)

Initializing audio output...
I2S audio output initialized successfully
...

================================================
  Ready! Waiting for Bluetooth connection...
================================================
```

### 2. スマホから接続

#### iPhone の場合

1. **設定** → **Bluetooth** を開く
2. デバイス一覧から **"Pico2W Audio Receiver"** を選択
3. ペアリング完了

#### Android の場合

1. **設定** → **接続済みのデバイス** → **新しいデバイスとペア設定する** を開く
2. デバイス一覧から **"Pico2W Audio Receiver"** を選択
3. ペアリング完了

### 3. 音楽を再生

スマホの音楽アプリ、YouTube、Spotify などから音楽を再生すると、Pico 2 W に接続したスピーカーから音が出ます。

### 4. ログの確認

接続が成功すると、以下のようなログが表示されます：

```
A2DP connection established: XX:XX:XX:XX:XX:XX (CID: 0x0040)
Stream established: XX:XX:XX:XX:XX:XX
SBC configuration received: channels 2, sample rate 44100 Hz
Stream started - Audio playback begins

>>> Audio stream connected!

[I2S] Buffer: 8960/44100 samples (20.3%) | Free: 35140 | Underruns: 0 | Overruns: 0
```

## カスタマイズ

### デバイス名の変更

`src/config.h` で変更できます。

```c
#define BT_DEVICE_NAME "My Custom Audio Receiver"
```

### サンプリングレートの変更

```c
#define AUDIO_SAMPLE_RATE    48000  // 44100 または 48000
```

### バッファサイズの調整

**重要**: RP2350のSRAMは264KBなので、バッファサイズには制限があります。

```c
// 大きいほど安定するが、遅延も増える
// 現在の推奨設定: 1秒分（安定性と低遅延のバランス）
#define AUDIO_BUFFER_SIZE    (AUDIO_SAMPLE_RATE * 1)  // 1秒分 = 44100サンプル

// DMA バッファサイズ（通常は変更不要）
#define DMA_BUFFER_SIZE      512  // 11.6ms分
```

**メモリ使用量の目安**:
- 1秒分: 約172 KB（推奨）
- 2秒分: 約344 KB（ギリギリ可能）
- 4秒分: 約689 KB（メモリオーバーフローで不可能）

## トラブルシューティング

### スマホから Pico 2 W が見えない

- Pico 2 W が正常に起動しているか確認（USB シリアルログをチェック）
- Pico 2 W の電源を入れ直す
- スマホの Bluetooth をオフ→オンする

### 接続できるが音が出ない

- 配線を確認（[WIRING.md](WIRING.md) 参照）
- I2S DAC の電源が入っているか確認
- スマホの音量を上げる
- USB シリアルログでエラーメッセージを確認

### 音が途切れる・ノイズが入る

**ソフトウェア側の確認**:
- USB シリアルログで「Underruns」「Overruns」「Dropped」をチェック
- バッファレベルが18-22%で安定しているか確認
- すべてゼロであればソフトウェアは正常動作中

**ハードウェア側の確認**（ソフトウェアメトリクスが正常な場合）:
- 電源の品質（USBハブ経由の場合、直接PCに接続を試す）
- GND配線の接続（Pico GNDとDAC GNDを確実に接続）
- DACモジュールの品質（安価なモジュールはノイズが多い）
- オーディオケーブルの品質とシールド
- スピーカー/アンプとの接続

**バッファ設定の調整**（上記で改善しない場合）:
- `config.h`で設定を調整可能
- ただし、現在の設定（1秒バッファ、20%自動開始）が最適化済み

### ビルドエラー

- Pico SDK のバージョンを確認（v1.5.0 以降が必要）
- `PICO_SDK_PATH` 環境変数が正しく設定されているか確認
- `git submodule update --init` を実行

## 技術詳細

### 現在の動作状態

**バッファ管理**:
- リングバッファ: 1秒分（44,100サンプル）
- DMAバッファ: 512サンプル（11.6ms）
- 自動開始閾値: 20%（8,820サンプル）
- 安定動作時のバッファレベル: 20-30%
- Underruns/Overruns/Dropped: すべて0で安定

**タイミング設定**:
- PIOクロック: 66サイクル/ステレオサンプル
- サンプリングレート: 44,100 Hz
- DMA割り込み優先度: 0xFF（最低、Bluetooth処理を優先）

**コード品質**:
- すべてのマジックナンバーを`config.h`に定数化
- 統一されたログ出力フォーマット
- 詳細なエラー検証とレポート

### アーキテクチャ

```
┌─────────────────┐
│  iPhone/Android │
│   (A2DP Source) │
└────────┬────────┘
         │ Bluetooth
         │ SBC Encoded Stream
         ▼
┌─────────────────────────────┐
│   Raspberry Pi Pico 2 W     │
│                             │
│  ┌──────────────────────┐   │
│  │ BTstack (A2DP Sink)  │   │
│  │  - SBC Decoder       │   │
│  └──────────┬───────────┘   │
│             │ PCM Data      │
│             ▼               │
│  ┌──────────────────────┐   │
│  │  Ring Buffer         │   │
│  │  (DMA)               │   │
│  └──────────┬───────────┘   │
│             ▼               │
│  ┌──────────────────────┐   │
│  │ I2S DAC / PWM Output │   │
│  └──────────┬───────────┘   │
└─────────────┼───────────────┘
              │ Analog Audio
              ▼
      ┌───────────────┐
      │  Speaker/DAC  │
      └───────────────┘
```

### 使用ライブラリ

- **BTstack**: Bluetooth スタック（A2DP Sink, SBC デコーダー）
- **Pico SDK**: ハードウェア制御（I2S, PWM, DMA, PIO）
- **CYW43 ドライバー**: Pico W の無線チップ制御

## 開発履歴と安定版

### 最新の安定版（推奨）

**現在のHEAD**（リファクタリング完了版）:
- **特徴**:
  - すべてのマジックナンバーを`config.h`に定数化
  - コードの可読性と保守性が大幅に向上
  - バッファ管理の最適化（18-22%で安定動作）
  - 詳細なログとエラーレポート
  - Underruns/Overruns/Droppedすべて0で安定
- **技術設定**:
  - 自動開始閾値: 20%（8,820サンプル）
  - リングバッファ: 1秒分（44,100サンプル）
  - DMAバッファ: 512サンプル
  - PIOクロック: 66サイクル/ステレオサンプル
  - DMA割り込み優先度: 0xFF（最低）

### 過去の主要コミット

**安定版コミット `842e9df`**（システムクロック最適化を戻した版）:
- **説明**: システムクロックをデフォルト150MHzに戻してBluetooth接続を修正
- **復元コマンド**:
  ```bash
  git checkout 842e9df
  # または新しいブランチで
  git checkout -b restore-842e9df 842e9df
  ```

**コミット `234a5fd`**（バッファオーバーフロー修正）:
- **説明**: AUTO_START_THRESHOLDの計算エラーを修正
- **修正内容**: `I2S_BUFFER_SIZE / 5` → `AUDIO_BUFFER_SIZE / 10`

**コミット `239aac5`**（バッファオーバーフロー原因特定）:
- **説明**: PIOクロック計算エラーを修正
- **修正内容**: 128サイクル → 66サイクル

**コミット `ed2b684`**（メモリオーバーフロー修正）:
- **説明**: バッファサイズを4秒から1秒に削減
- **理由**: RP2350のSRAM制限（264KB）に対応

### 復元方法

```bash
# 最新版を使用（推奨）
git checkout main  # またはメインブランチ名
git pull

# 特定のコミットに戻す
git checkout <コミットID>

# ビルドディレクトリをクリーンアップしてビルド
rm -rf build
mkdir build
cd build
cmake ..
make -j4
```

### トラブルシューティング

復元後にビルドエラーが出る場合：

```bash
# ビルドディレクトリをクリーン
cd build
rm -rf *
cmake ..
ninja
```

## ライセンス

MIT License

## 参考資料

- [Raspberry Pi Pico SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)
- [BTstack Documentation](https://bluekitchen-gmbh.com/btstack/)
- [A2DP Profile Specification](https://www.bluetooth.com/specifications/specs/advanced-audio-distribution-profile-1-3/)

## 貢献

バグ報告や機能リクエストは、GitHub Issues でお願いします。
