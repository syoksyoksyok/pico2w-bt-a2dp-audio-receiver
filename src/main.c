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

    static uint32_t pcm_total_count = 0;
    static uint32_t pcm_total_samples = 0;
    static uint32_t pcm_dropped = 0;

    pcm_total_count++;
    pcm_total_samples += num_samples;

    // I2S 出力にPCMデータを書き込み
    uint32_t written = audio_out_i2s_write(pcm_data, num_samples);

    if (written < num_samples) {
        uint32_t dropped = num_samples - written;
        pcm_dropped += dropped;
#ifdef ENABLE_DEBUG_LOG
        printf("WARNING: Audio buffer full, dropped %lu samples (total dropped: %lu)\n",
               dropped, pcm_dropped);
#endif
    }

    // 100回ごとに統計を表示
    if (pcm_total_count % 100 == 0) {
        printf("[PCM Stats] Callbacks: %lu, Total samples: %lu, Dropped: %lu\n",
               pcm_total_count, pcm_total_samples, pcm_dropped);
    }
}

// ============================================================================
// バッファ状態のログ出力
// ============================================================================

static void log_buffer_status(void) {
    absolute_time_t now = get_absolute_time();
    int64_t elapsed_ms = absolute_time_diff_us(last_status_log_time, now) / 1000;

    if (elapsed_ms < BUFFER_STATUS_LOG_INTERVAL_MS) {
        return;
    }

    last_status_log_time = now;

    // I2Sバッファ状態を取得して表示
    uint32_t buffered = audio_out_i2s_get_buffered_samples();
    uint32_t free_space = audio_out_i2s_get_free_space();
    uint32_t underruns, overruns;
    audio_out_i2s_get_stats(&underruns, &overruns);

    printf("[I2S] Buffer: %lu/%u samples | Free: %lu | Underruns: %lu | Overruns: %lu\n",
           buffered, AUDIO_BUFFER_SIZE, free_space, underruns, overruns);

    // バッファ状態の警告
    if (buffered < BUFFER_LOW_THRESHOLD) {
        printf("  WARNING: Buffer level low!\n");
    } else if (buffered > BUFFER_HIGH_THRESHOLD) {
        printf("  WARNING: Buffer level high!\n");
    }
}

// ============================================================================
// メイン関数
// ============================================================================

int main(void) {
    // システムクロックを最適化（I2S用に正確な整数分周を実現）
    // 44.1kHz × 66サイクル = 2,910,600 Hz が必要
    // 151.35 MHz / 52 = 2,910,600 Hz （完全な整数分周！）
    set_sys_clock_khz(151351, true);

    // 標準入出力の初期化
    stdio_init_all();

    // 起動メッセージ
    sleep_ms(2000);  // USB シリアル接続の安定化待ち

    printf("\n");
    printf("================================================\n");
    printf("  Pico 2W Bluetooth A2DP Audio Receiver\n");
    printf("================================================\n");
    printf("\n");

    // 設定情報を表示
    printf("Configuration:\n");
    printf("  System clock: %lu Hz (%.2f MHz) - optimized for I2S\n",
           clock_get_hz(clk_sys), clock_get_hz(clk_sys) / 1000000.0f);
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

        // 接続状態の監視
        bool is_connected = bt_audio_is_connected();

        if (is_connected && !was_connected) {
            printf("\n>>> Audio stream connected!\n\n");
            was_connected = true;
        } else if (!is_connected && was_connected) {
            printf("\n>>> Audio stream disconnected\n\n");

            // バッファをクリア
            audio_out_i2s_clear_buffer();

            was_connected = false;
        }

        // バッファ状態のログ出力（定期的）
#ifdef ENABLE_DEBUG_LOG
        if (is_connected) {
            log_buffer_status();
        }
#endif

        // CPU負荷軽減のため、tight_loop_contents()を使用
        // sleep_ms(1)は使わない！→ BTstack/CYW43の割り込み処理を遅延させて切断の原因になる
        tight_loop_contents();  // 割り込みを許可しながら効率的に待機
    }

    return 0;
}
