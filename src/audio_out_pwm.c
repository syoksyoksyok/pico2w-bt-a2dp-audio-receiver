/**
 * @file audio_out_pwm.c
 * @brief PWM 簡易 DAC オーディオ出力モジュール（DMA + リングバッファ）
 */

#include "audio_out_pwm.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

// ============================================================================
// 内部変数
// ============================================================================

static uint slice_num;
static uint channel;
static int dma_channel = -1;

static uint32_t sample_rate_hz = 44100;

// リングバッファ（モノラル）
#define PWM_BUFFER_SIZE (AUDIO_BUFFER_SIZE)
static uint8_t ring_buffer[PWM_BUFFER_SIZE];
static volatile uint32_t write_pos = 0;
static volatile uint32_t read_pos = 0;
static volatile uint32_t buffered_samples = 0;

// DMA バッファ
#define PWM_DMA_BUFFER_SIZE 512
static uint8_t dma_buffer[PWM_DMA_BUFFER_SIZE];

// 統計情報
static uint32_t underrun_count = 0;
static uint32_t overrun_count = 0;

// 状態
static bool is_running = false;

// ============================================================================
// 内部関数（前方宣言）
// ============================================================================

static void dma_handler(void);
static void fill_dma_buffer(uint8_t *buffer, uint32_t num_samples);

// ============================================================================
// PWM オーディオ出力の初期化
// ============================================================================

bool audio_out_pwm_init(uint32_t sample_rate) {
    printf("Initializing PWM audio output...\n");
    printf("  Sample rate: %lu Hz\n", sample_rate);
    printf("  Resolution: %d bits\n", PWM_RESOLUTION_BITS);
    printf("  Output pin: GPIO %d\n", PWM_AUDIO_PIN);

    sample_rate_hz = sample_rate;

    // PWM ピンの初期化
    gpio_set_function(PWM_AUDIO_PIN, GPIO_FUNC_PWM);

    // PWM スライスとチャンネルを取得
    slice_num = pwm_gpio_to_slice_num(PWM_AUDIO_PIN);
    channel = pwm_gpio_to_channel(PWM_AUDIO_PIN);

    // PWM 設定
    pwm_config config = pwm_get_default_config();

    // PWM の周波数を設定
    // PWM 周波数 = システムクロック / (wrap + 1) / div
    // サンプリングレートに合わせる
    uint32_t wrap = (1 << PWM_RESOLUTION_BITS) - 1;  // 8bit なら 255
    float div = (float)clock_get_hz(clk_sys) / (sample_rate * (wrap + 1));

    pwm_config_set_clkdiv(&config, div);
    pwm_config_set_wrap(&config, wrap);

    pwm_init(slice_num, &config, true);

    // DMA チャンネルを取得
    dma_channel = dma_claim_unused_channel(true);

    printf("  DMA channel: %d\n", dma_channel);
    printf("  PWM slice: %d, channel: %d\n", slice_num, channel);

    // DMA の設定
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, DREQ_PWM_WRAP0 + slice_num);

    dma_channel_configure(
        dma_channel,
        &c,
        &pwm_hw->slice[slice_num].cc,  // PWM カウンタ比較レジスタ
        dma_buffer,
        PWM_DMA_BUFFER_SIZE,
        false
    );

    // DMA 割り込みハンドラーを設定
    dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // バッファをクリア
    audio_out_pwm_clear_buffer();

    printf("PWM audio output initialized successfully\n");

    return true;
}

// ============================================================================
// PCM データをバッファに書き込む
// ============================================================================

uint32_t audio_out_pwm_write(const int16_t *pcm_data, uint32_t num_samples, uint8_t channels) {
    uint32_t samples_written = 0;

    for (uint32_t i = 0; i < num_samples; i++) {
        uint32_t free_space = PWM_BUFFER_SIZE - buffered_samples;

        if (free_space == 0) {
            // バッファがいっぱい（オーバーラン）
            overrun_count++;
            break;
        }

        // 16bit signed PCM を 8bit unsigned PWM 値に変換
        int16_t sample;

        if (channels == 2) {
            // ステレオの場合、L と R を平均化してモノラルに変換
            int32_t left = pcm_data[i * 2];
            int32_t right = pcm_data[i * 2 + 1];
            sample = (int16_t)((left + right) / 2);
        } else {
            // モノラル
            sample = pcm_data[i];
        }

        // -32768 ~ 32767 を 0 ~ 255 に変換
        uint8_t pwm_value = (uint8_t)((sample + 32768) >> (16 - PWM_RESOLUTION_BITS));

        ring_buffer[write_pos] = pwm_value;
        write_pos = (write_pos + 1) % PWM_BUFFER_SIZE;
        buffered_samples++;
        samples_written++;
    }

    return samples_written;
}

// ============================================================================
// バッファの空き容量を取得
// ============================================================================

uint32_t audio_out_pwm_get_free_space(void) {
    return PWM_BUFFER_SIZE - buffered_samples;
}

// ============================================================================
// バッファ内のデータ量を取得
// ============================================================================

uint32_t audio_out_pwm_get_buffered_samples(void) {
    return buffered_samples;
}

// ============================================================================
// オーディオ出力を開始
// ============================================================================

void audio_out_pwm_start(void) {
    if (is_running) return;

    printf("Starting PWM audio output...\n");

    // 最初の DMA バッファを埋める
    fill_dma_buffer(dma_buffer, PWM_DMA_BUFFER_SIZE);

    // DMA を開始
    dma_channel_start(dma_channel);

    // PWM を開始
    pwm_set_enabled(slice_num, true);

    is_running = true;
    printf("PWM audio output started\n");
}

// ============================================================================
// オーディオ出力を停止
// ============================================================================

void audio_out_pwm_stop(void) {
    if (!is_running) return;

    printf("Stopping PWM audio output...\n");

    // PWM を停止
    pwm_set_enabled(slice_num, false);

    // DMA を停止
    dma_channel_abort(dma_channel);

    is_running = false;
    printf("PWM audio output stopped\n");
}

// ============================================================================
// バッファをクリア
// ============================================================================

void audio_out_pwm_clear_buffer(void) {
    write_pos = 0;
    read_pos = 0;
    buffered_samples = 0;
    underrun_count = 0;
    overrun_count = 0;

    // 中間値（128）で埋める
    memset(ring_buffer, 128, sizeof(ring_buffer));
    memset(dma_buffer, 128, sizeof(dma_buffer));
}

// ============================================================================
// バッファ統計情報を取得
// ============================================================================

void audio_out_pwm_get_stats(uint32_t *underruns, uint32_t *overruns) {
    if (underruns) *underruns = underrun_count;
    if (overruns) *overruns = overrun_count;
}

// ============================================================================
// DMA バッファを埋める
// ============================================================================

static void fill_dma_buffer(uint8_t *buffer, uint32_t num_samples) {
    for (uint32_t i = 0; i < num_samples; i++) {
        if (buffered_samples > 0) {
            // リングバッファからデータを読み出す
            buffer[i] = ring_buffer[read_pos];

            read_pos = (read_pos + 1) % PWM_BUFFER_SIZE;
            buffered_samples--;
        } else {
            // データがない場合は無音（中間値）を出力（アンダーラン）
            buffer[i] = 128;
            underrun_count++;
        }
    }
}

// ============================================================================
// DMA 割り込みハンドラー
// ============================================================================

static void dma_handler(void) {
    if (dma_channel_get_irq0_status(dma_channel)) {
        dma_channel_acknowledge_irq0(dma_channel);

        // DMA バッファを再充填
        fill_dma_buffer(dma_buffer, PWM_DMA_BUFFER_SIZE);

        // DMA を再起動
        dma_channel_set_read_addr(dma_channel, dma_buffer, true);
    }
}
