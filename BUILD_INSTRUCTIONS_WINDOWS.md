# Windowsでのビルド手順

## 前提条件

1. Pico SDKがインストールされていること（例: `C:\pico\pico-sdk`）
2. arm-none-eabi-gcc がインストールされていること（MSYS2経由など）
3. CMake、Ninja がインストールされていること

## 1. 正しいブランチに切り替え

```cmd
cd C:\Users\Administrator\Documents\Arduino\Raspberry_Pi_Pico_2_W\pico2w-bt-a2dp-audio-receiver

# 現在のブランチを確認
git branch

# 修正を含むブランチに切り替え
git checkout claude/fix-bluetooth-connection-018HaavzJzk7hdQZpFfrxxty

# リモートから最新を取得
git pull origin claude/fix-bluetooth-connection-018HaavzJzk7hdQZpFfrxxty
```

## 2. ビルドディレクトリのクリーンアップ

```cmd
# 既存のbuildディレクトリがあれば削除
cd C:\Users\Administrator\Documents\Arduino\Raspberry_Pi_Pico_2_W\pico2w-bt-a2dp-audio-receiver
rmdir /s /q build
mkdir build
cd build
```

## 3. CMake設定（Ninjaビルドシステムを使用）

```cmd
cmake -G Ninja -DPICO_BOARD=pico2_w ..
```

または、すでに実行済みであればスキップしてOKです。

## 4. ビルド実行（Ninjaを使用）

```cmd
# makeではなくninjaを使用
ninja

# または並列ビルド（推奨）
ninja -j4
```

## 5. ビルド成果物の確認

```cmd
dir pico2w_bt_a2dp_audio_receiver.uf2
```

成功すると以下のファイルが生成されます：
- `pico2w_bt_a2dp_audio_receiver.uf2` ← これをPico 2Wに書き込む
- `pico2w_bt_a2dp_audio_receiver.elf`
- `pico2w_bt_a2dp_audio_receiver.bin`

## 6. Pico 2Wへの書き込み

1. **Pico 2WのBOOTSELボタンを押しながらUSB接続**
2. Pico 2Wが `RPI-RP2` というドライブとしてマウントされる
3. 生成された `.uf2` ファイルをドラッグ&ドロップ

```cmd
# またはコマンドでコピー
copy pico2w_bt_a2dp_audio_receiver.uf2 E:\
# （Eドライブは環境に応じて変更してください）
```

## 7. シリアルモニタでログ確認

### TeraTerm を使用する場合

1. TeraTerm を起動
2. 接続先のCOMポート（例: COM3）を選択
3. ボーレート: **115200**
4. データビット: 8、ストップビット: 1、パリティ: なし

### PuTTYを使用する場合

1. PuTTY を起動
2. Connection Type: Serial
3. Serial line: COM3（デバイスマネージャーで確認）
4. Speed: 115200

### Arduino IDE のシリアルモニタを使用する場合

1. Arduino IDE を起動
2. ツール → シリアルポート → COMポートを選択
3. ツール → シリアルモニタを開く
4. ボーレート: 115200 に設定

## 期待されるログ出力

```
========================================
Pico 2W Bluetooth A2DP Audio Receiver
========================================
Initializing Bluetooth...
CYW43 initialized
Initializing BTstack run loop...    ← 新規追加（修正済み）
Initializing HCI transport...       ← 新規追加（修正済み）
HCI event handler registered        ← 新規追加（修正済み）
Bluetooth A2DP Sink initialized successfully
Device name: Pico2W-A2DP
Waiting for connection...
========================================
```

## トラブルシューティング

### エラー: "No targets specified and no makefile found"

CMakeがNinjaを使用しているので、`make`ではなく`ninja`を使用してください：
```cmd
ninja -j4
```

### エラー: ninja: command not found

Ninjaがインストールされていない場合は、makefileベースのビルドに変更：
```cmd
# buildディレクトリを削除して再設定
cd ..
rmdir /s /q build
mkdir build
cd build

# Unix Makefilesを使用
cmake -G "Unix Makefiles" -DPICO_BOARD=pico2_w ..
make -j4
```

### COMポートが見つからない

デバイスマネージャーで確認：
1. Windowsキー + X → デバイスマネージャー
2. ポート（COMとLPT）を展開
3. "USB Serial Device (COMx)" を確認

### ブランチの一覧を確認

```cmd
# ローカルブランチを確認
git branch

# リモートブランチも含めて確認
git branch -a

# 修正ブランチに切り替え
git checkout claude/fix-bluetooth-connection-018HaavzJzk7hdQZpFfrxxty
```
