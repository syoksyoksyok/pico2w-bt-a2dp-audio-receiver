cmake_minimum_required(VERSION 3.13)

# Pico 2W (RP2350 + CYW43) 設定
# SDK 2.0にはpico2_w定義がないため、pico_wボードでrp2350プラットフォームを使用
set(PICO_BOARD pico_w)
set(PICO_PLATFORM rp2350)

# Pico SDK のパスを設定（環境変数 PICO_SDK_PATH から取得）
set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

# BTstack を有効化
set(PICO_BTSTACK_PATH ${PICO_SDK_PATH}/lib/btstack)

# Pico SDK を初期化
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico2w_bt_a2dp_receiver C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pico SDK を初期化
pico_sdk_init()

# 実行ファイルを作成
add_executable(pico2w_bt_a2dp_receiver
    src/main.c
    src/bt_audio.c
    src/audio_out_i2s.c
    src/audio_out_pwm.c
)

# インクルードディレクトリ
target_include_directories(pico2w_bt_a2dp_receiver PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# リンクするライブラリ
target_link_libraries(pico2w_bt_a2dp_receiver
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_cyw43
    pico_btstack_sbc_encoder
    pico_btstack_sbc_decoder
    hardware_i2s
    hardware_dma
    hardware_pwm
    hardware_pio
    hardware_clocks
)

# USB シリアル出力を有効化（デバッグログ用）
pico_enable_stdio_usb(pico2w_bt_a2dp_receiver 1)
pico_enable_stdio_uart(pico2w_bt_a2dp_receiver 0)

# UF2 ファイルを生成
pico_add_extra_outputs(pico2w_bt_a2dp_receiver)

# ビルドオプション（最適化）
target_compile_options(pico2w_bt_a2dp_receiver PRIVATE
    -Wall
    -Wextra
    -O2
)
