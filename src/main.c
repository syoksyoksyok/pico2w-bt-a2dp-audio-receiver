/**
 * @file main.c
 * @brief Pico 2W Bluetooth A2DP Audio Receiver - メインプログラム
 *
 * iPhone/Android スマホから Bluetooth (A2DP) で音声を受信し、
 * I2S DAC または PWM で再生するプログラム
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "config.h"
#include "bt_audio.h"

#ifdef USE_I2S_OUTPUT
#include "audio_out_i2s.h"
#endif

#ifdef USE_PWM_OUTPUT
#include "audio_out_pwm.h"
#endif

// ============================================================================
// グローバル変数
// ============================================================================

static absolute_time_t last_status_log_time;

// ============================================================================
// PCM データ受信コールバック
// ============================================================================

static void pcm_data_handler(const int16_t *pcm_data, uint32_t num_samples,
                              uint8_t channels, uint32_t sample_rate) {
#ifdef USE_I2S_OUTPUT
    // I2S 出力モードの場合
    uint32_t written = audio_out_i2s_write(pcm_data, num_samples);

    if (written < num_samples) {
#ifdef ENABLE_DEBUG_LOG
        printf("WARNING: Audio buffer full, dropped %lu samples\n", num_samples - written);
#endif
    }

#elif defined(USE_PWM_OUTPUT)
    // PWM 出力モードの場合
    uint32_t written = audio_out_pwm_write(pcm_data, num_samples, channels);

    if (written < num_samples) {
#ifdef ENABLE_DEBUG_LOG
        printf("WARNING: Audio buffer full, dropped %lu samples\n", num_samples - written);
#endif
    }
#endif
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

#ifdef USE_I2S_OUTPUT
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

#elif defined(USE_PWM_OUTPUT)
    uint32_t buffered = audio_out_pwm_get_buffered_samples();
    uint32_t free_space = audio_out_pwm_get_free_space();
    uint32_t underruns, overruns;
    audio_out_pwm_get_stats(&underruns, &overruns);

    printf("[PWM] Buffer: %lu samples | Free: %lu | Underruns: %lu | Overruns: %lu\n",
           buffered, free_space, underruns, overruns);

    // バッファ状態の警告
    if (buffered < BUFFER_LOW_THRESHOLD) {
        printf("  WARNING: Buffer level low!\n");
    } else if (buffered > BUFFER_HIGH_THRESHOLD) {
        printf("  WARNING: Buffer level high!\n");
    }
#endif
}

// ============================================================================
// メイン関数
// ============================================================================

int main(void) {
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
    printf("  Device name: %s\n", BT_DEVICE_NAME);

#ifdef USE_I2S_OUTPUT
    printf("  Output mode: I2S DAC\n");
    printf("  I2S pins: DATA=%d, BCLK=%d, LRCLK=%d\n",
           I2S_DATA_PIN, I2S_BCLK_PIN, I2S_LRCLK_PIN);
#elif defined(USE_PWM_OUTPUT)
    printf("  Output mode: PWM (simple DAC)\n");
    printf("  PWM pin: %d\n", PWM_AUDIO_PIN);
#endif

    printf("  Sample rate: %d Hz\n", AUDIO_SAMPLE_RATE);
    printf("  Channels: %d\n", AUDIO_CHANNELS);
    printf("  Buffer size: %d samples\n", AUDIO_BUFFER_SIZE);
    printf("\n");

    // オーディオ出力の初期化
    printf("Initializing audio output...\n");

#ifdef USE_I2S_OUTPUT
    if (!audio_out_i2s_init(AUDIO_SAMPLE_RATE, AUDIO_BITS_PER_SAMPLE, AUDIO_CHANNELS)) {
        printf("ERROR: Failed to initialize I2S audio output\n");
        return 1;
    }
    audio_out_i2s_start();
#elif defined(USE_PWM_OUTPUT)
    if (!audio_out_pwm_init(AUDIO_SAMPLE_RATE)) {
        printf("ERROR: Failed to initialize PWM audio output\n");
        return 1;
    }
    audio_out_pwm_start();
#endif

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
#ifdef USE_I2S_OUTPUT
            audio_out_i2s_clear_buffer();
#elif defined(USE_PWM_OUTPUT)
            audio_out_pwm_clear_buffer();
#endif

            was_connected = false;
        }

        // バッファ状態のログ出力（定期的）
#ifdef ENABLE_DEBUG_LOG
        if (is_connected) {
            log_buffer_status();
        }
#endif

        // 短いスリープ（CPU 負荷軽減）
        sleep_ms(1);
    }

    return 0;
}
