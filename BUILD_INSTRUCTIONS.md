# ビルド手順

## 前提条件

1. Pico SDKがインストールされていること
2. CMake、GCCなどのビルドツールがインストールされていること

## 1. ブランチの確認と最新化

```bash
# 現在のブランチを確認
git status

# リモートから最新を取得（必要に応じて）
git pull origin claude/fix-bluetooth-connection-018HaavzJzk7hdQZpFfrxxty
```

## 2. 環境変数の設定

Pico SDKのパスを設定します（既に設定済みの場合はスキップ）：

```bash
# Pico SDKのパスを設定（実際のパスに合わせて変更してください）
export PICO_SDK_PATH=/path/to/pico-sdk

# 例：
# export PICO_SDK_PATH=~/pico/pico-sdk
```

## 3. ビルドディレクトリのクリーンアップ（推奨）

```bash
# 既存のビルドディレクトリを削除して、クリーンビルド
rm -rf build
mkdir build
cd build
```

## 4. CMake設定

```bash
# Pico 2Wの場合
cmake -DPICO_BOARD=pico2_w ..

# または、環境変数で設定する場合
export PICO_BOARD=pico2_w
cmake ..
```

## 5. ビルド実行

```bash
# 並列ビルド（4コアの場合）
make -j4

# またはシングルコア
make
```

## 6. ビルド成果物の確認

ビルドが成功すると、以下のファイルが生成されます：

```bash
build/pico2w_bt_a2dp_audio_receiver.uf2  # Pico 2Wに書き込むファイル
build/pico2w_bt_a2dp_audio_receiver.elf  # デバッグ用
build/pico2w_bt_a2dp_audio_receiver.bin  # バイナリファイル
```

## 7. Pico 2Wへの書き込み

1. Pico 2WのBOOTSELボタンを押しながらUSBケーブルを接続
2. Pico 2WがUSBドライブとしてマウントされる
3. 生成された `.uf2` ファイルをドラッグ&ドロップ

```bash
# Linuxの場合の例（自動マウントされたパスに合わせて変更）
cp build/pico2w_bt_a2dp_audio_receiver.uf2 /media/$USER/RPI-RP2/
```

4. 書き込みが完了すると自動的にPicoが再起動します

## 8. シリアルモニタでログ確認

```bash
# Linuxの場合
sudo minicom -D /dev/ttyACM0 -b 115200

# またはscreenを使用
sudo screen /dev/ttyACM0 115200

# macOSの場合
screen /dev/cu.usbmodem* 115200
```

期待されるログ出力：
```
========================================
Pico 2W Bluetooth A2DP Audio Receiver
========================================
Initializing Bluetooth...
CYW43 initialized
Initializing BTstack run loop...
Initializing HCI transport...
HCI event handler registered
Bluetooth A2DP Sink initialized successfully
Device name: Pico2W-A2DP
Waiting for connection...
========================================
```

## トラブルシューティング

### CMakeエラー: pico_sdk_import.cmake not found

```bash
# PICO_SDK_PATHが設定されているか確認
echo $PICO_SDK_PATH

# 設定されていない場合は設定
export PICO_SDK_PATH=/path/to/pico-sdk
```

### ビルドエラー: pico_btstack_ble library not found

Pico SDKのバージョンが古い可能性があります。最新版に更新してください：

```bash
cd $PICO_SDK_PATH
git pull
git submodule update --init --recursive
```

### シリアルポートが見つからない

```bash
# デバイスの確認
ls -l /dev/ttyACM* /dev/ttyUSB*

# または
dmesg | grep tty
```
