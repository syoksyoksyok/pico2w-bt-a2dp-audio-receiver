/**
 * @file main.c
 * @brief Pico 2W Bluetooth A2DP Audio Receiver - メインプログラム
 *
 * iPhone/Android スマホから Bluetooth (A2DP) で音声を受信し、
 * I2S DAC で再生するプログラム（I2S専用）
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "config.h"
#include "bt_audio.h"
#include "audio_out_i2s.h"

// ============================================================================
// グローバル変数
// ============================================================================

static absolute_time_t last_status_log_time;

// ============================================================================
// PCM データ受信コールバック
// ============================================================================

static void pcm_data_handler(const int16_t *pcm_data, uint32_t num_samples,
                              uint8_t channels, uint32_t sample_rate) {
    (void)channels;      // I2Sはステレオ固定
    (void)sample_rate;   // サンプルレートは設定済み

    // I2S 出力にPCMデータを書き込み
    (void)audio_out_i2s_write(pcm_data, num_samples);
}

// ============================================================================
// Bluetooth状態変化時のオーディオバッファ制御
// ============================================================================

static void bt_control_handler(bt_audio_control_event_t event, uint16_t cid) {
    switch (event) {
        case BT_AUDIO_CONTROL_STREAM_STOPPED:
        case BT_AUDIO_CONTROL_STREAM_RELEASED:
        case BT_AUDIO_CONTROL_CONNECTION_RELEASED:
        case BT_AUDIO_CONTROL_TAKEOVER_STARTED:
        case BT_AUDIO_CONTROL_STREAM_STARTING:
            printf("[I2S] Reset audio buffers for BT event %d (CID: 0x%04x)\n", event, cid);
            audio_out_i2s_stop();
            audio_out_i2s_clear_buffer();
            break;
        default:
            break;
    }
}

// ============================================================================
// バッファ状態のログ出力
// ============================================================================

#if ENABLE_DEBUG_LOG
static void log_buffer_status(void) {
    absolute_time_t now = get_absolute_time();
    int64_t elapsed_ms = absolute_time_diff_us(last_status_log_time, now) / 1000;

    if (elapsed_ms < BUFFER_STATUS_LOG_INTERVAL_MS) {
        return;
    }

    last_status_log_time = now;

    // I2Sバッファ状態を取得して表示
    audio_out_i2s_debug_stats_t stats;
    audio_out_i2s_get_debug_stats(&stats);

    printf("[I2S] Buffer: %lu/%u samples | Free: %lu | Min: %lu | Max: %lu | Underruns: %lu | Overruns: %lu | Dropped: %lu | Rebuffer: %lu | DMA switches: %lu | DMA A/B: %u/%u | running=%u rebuffering=%u\n",
           stats.buffered_samples, AUDIO_BUFFER_SIZE, stats.free_samples,
           stats.min_buffered_samples, stats.max_buffered_samples,
           stats.underruns, stats.overruns, stats.dropped_samples,
           stats.rebuffer_count, stats.dma_switch_count,
           stats.dma_buffer_state[0], stats.dma_buffer_state[1],
           stats.running ? 1u : 0u, stats.rebuffering ? 1u : 0u);

    // バッファ状態の警告
    if (stats.buffered_samples < BUFFER_LOW_THRESHOLD) {
        printf("  WARNING: Buffer level low!\n");
    } else if (stats.buffered_samples > BUFFER_HIGH_THRESHOLD) {
        printf("  WARNING: Buffer level high!\n");
    }
}
#endif

// ============================================================================
// メイン関数
// ============================================================================

int main(void) {
    // 標準入出力の初期化
    stdio_init_all();

    // 起動メッセージ
    sleep_ms(USB_SERIAL_STABILIZATION_MS);  // USB シリアル接続の安定化待ち

    printf("\n");
    printf("================================================\n");
    printf("  Pico 2W Bluetooth A2DP Audio Receiver\n");
    printf("================================================\n");
    printf("\n");

    // 設定情報を表示
    printf("Configuration:\n");
    printf("  Device name: %s\n", BT_DEVICE_NAME);
    printf("  Output mode: I2S DAC\n");
    printf("  I2S pins: DATA=%d, BCLK=%d, LRCLK=%d\n",
           I2S_DATA_PIN, I2S_BCLK_PIN, I2S_LRCLK_PIN);
    printf("  Sample rate: %d Hz\n", AUDIO_SAMPLE_RATE);
    printf("  Channels: %d (Stereo)\n", AUDIO_CHANNELS);
    printf("  Buffer size: %d samples\n", AUDIO_BUFFER_SIZE);
    printf("\n");

    // オーディオ出力の初期化
    printf("Initializing I2S audio output...\n");

    if (!audio_out_i2s_init(AUDIO_SAMPLE_RATE, AUDIO_BITS_PER_SAMPLE, AUDIO_CHANNELS)) {
        printf("ERROR: Failed to initialize I2S audio output\n");
        return 1;
    }
    // 注意: audio_out_i2s_start() はバッファが十分に埋まったら自動的に開始されます

    printf("\n");

    // Bluetooth A2DP の初期化
    if (!bt_audio_init()) {
        printf("ERROR: Failed to initialize Bluetooth A2DP\n");
        return 1;
    }

    // PCM データコールバックを設定
    bt_audio_set_pcm_callback(pcm_data_handler);
    bt_audio_set_control_callback(bt_control_handler);

    printf("\n");
    printf("================================================\n");
    printf("  Ready! Waiting for Bluetooth connection...\n");
    printf("================================================\n");
    printf("\n");
    printf("Connect from your smartphone:\n");
    printf("  1. Open Bluetooth settings on your phone\n");
    printf("  2. Look for '%s'\n", BT_DEVICE_NAME);
    printf("  3. Tap to connect\n");
    printf("  4. Play audio from your phone\n");
    printf("\n");

    // 最後のログ時刻を初期化
    last_status_log_time = get_absolute_time();

    // メインループ
    bool was_connected = false;

    while (true) {
        // Bluetooth スタックの実行
        bt_audio_run();

        // DMAバッファ補充など、割り込み外で実行するI2S処理
        audio_out_i2s_process();

        // 接続状態の監視
        bool is_connected = bt_audio_is_connected();

        if (is_connected && !was_connected) {
            printf("\n>>> Audio stream connected!\n\n");
            was_connected = true;
        } else if (!is_connected && was_connected) {
            printf("\n>>> Audio stream disconnected\n\n");

            // DMA/PIOを停止してからバッファをクリア
            audio_out_i2s_stop();
            audio_out_i2s_clear_buffer();

            was_connected = false;
        }

        // バッファ状態のログ出力（定期的）
#if ENABLE_DEBUG_LOG
        if (is_connected) {
            log_buffer_status();
            bt_audio_debug_log_process();
        }
#endif

        // CPU負荷軽減のため、tight_loop_contents()を使用
        // sleep_ms(1)は使わない！→ BTstack/CYW43の割り込み処理を遅延させて切断の原因になる
        tight_loop_contents();  // 割り込みを許可しながら効率的に待機
    }

    return 0;
}
