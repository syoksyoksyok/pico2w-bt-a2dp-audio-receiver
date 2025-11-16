/**
 * @file audio_out_i2s.c
 * @brief I2S DAC オーディオ出力モジュール（DMA + リングバッファ）
 */

#include "audio_out_i2s.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

// ============================================================================
// I2S PIO プログラム（手動定義）
// ============================================================================

// I2S 送信用の簡易 PIO プログラム
// BCLK と LRCLK を生成し、DATA を送信する

#define i2s_offset 0

// PIO ステートマシン用の I2S プログラム
static const uint16_t i2s_program_instructions[] = {
    0x6008, // out pins, 8        ; 0: Output 8 bits to DATA pin
    0x0000, // jmp 0               ; 1: Loop
};

static const struct pio_program i2s_program = {
    .instructions = i2s_program_instructions,
    .length = 2,
    .origin = -1,
};

// ============================================================================
// 内部変数
// ============================================================================

static PIO pio = pio0;
static uint sm = 0;
static int dma_channel_0 = -1;
static int dma_channel_1 = -1;

static uint32_t sample_rate_hz = 44100;
static uint8_t bits_per_sample_val = 16;
static uint8_t num_channels = 2;

// リングバッファ
static int16_t ring_buffer[AUDIO_BUFFER_SIZE * 2];  // ステレオなので *2
static volatile uint32_t write_pos = 0;
static volatile uint32_t read_pos = 0;
static volatile uint32_t buffered_samples = 0;

// DMA バッファ（2つのバッファでピンポン転送）
static int16_t dma_buffer_0[DMA_BUFFER_SIZE * 2];
static int16_t dma_buffer_1[DMA_BUFFER_SIZE * 2];

// 統計情報
static uint32_t underrun_count = 0;
static uint32_t overrun_count = 0;

// 状態
static bool is_running = false;

// ============================================================================
// 内部関数（前方宣言）
// ============================================================================

static void dma_handler(void);
static void fill_dma_buffer(int16_t *buffer, uint32_t num_samples);
static void setup_pio_i2s(uint32_t sample_rate, uint8_t bits_per_sample);

// ============================================================================
// I2S オーディオ出力の初期化
// ============================================================================

bool audio_out_i2s_init(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels) {
    printf("Initializing I2S audio output...\n");
    printf("  Sample rate: %lu Hz\n", sample_rate);
    printf("  Bits per sample: %u\n", bits_per_sample);
    printf("  Channels: %u\n", channels);

    sample_rate_hz = sample_rate;
    bits_per_sample_val = bits_per_sample;
    num_channels = channels;

    // GPIO を I2S ピンとして初期化
    gpio_set_function(I2S_DATA_PIN, GPIO_FUNC_PIO0);
    gpio_set_function(I2S_BCLK_PIN, GPIO_FUNC_PIO0);
    gpio_set_function(I2S_LRCLK_PIN, GPIO_FUNC_PIO0);

    // PIO I2S のセットアップ
    setup_pio_i2s(sample_rate, bits_per_sample);

    // DMA チャンネルを取得
    dma_channel_0 = dma_claim_unused_channel(true);
    dma_channel_1 = dma_claim_unused_channel(true);

    printf("  DMA channels: %d, %d\n", dma_channel_0, dma_channel_1);

    // DMA チャンネル 0 の設定
    dma_channel_config c0 = dma_channel_get_default_config(dma_channel_0);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_16);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c0, dma_channel_1);

    dma_channel_configure(
        dma_channel_0,
        &c0,
        &pio->txf[sm],
        dma_buffer_0,
        DMA_BUFFER_SIZE * num_channels,
        false
    );

    // DMA チャンネル 1 の設定
    dma_channel_config c1 = dma_channel_get_default_config(dma_channel_1);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_16);
    channel_config_set_read_increment(&c1, true);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_dreq(&c1, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c1, dma_channel_0);

    dma_channel_configure(
        dma_channel_1,
        &c1,
        &pio->txf[sm],
        dma_buffer_1,
        DMA_BUFFER_SIZE * num_channels,
        false
    );

    // DMA 割り込みハンドラーを設定
    dma_channel_set_irq0_enabled(dma_channel_0, true);
    dma_channel_set_irq0_enabled(dma_channel_1, true);

    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // バッファをクリア
    audio_out_i2s_clear_buffer();

    printf("I2S audio output initialized successfully\n");
    printf("  I2S pins - DATA: %d, BCLK: %d, LRCLK: %d\n",
           I2S_DATA_PIN, I2S_BCLK_PIN, I2S_LRCLK_PIN);

    return true;
}

// ============================================================================
// PIO I2S のセットアップ
// ============================================================================

static void setup_pio_i2s(uint32_t sample_rate, uint8_t bits_per_sample) {
    // PIO プログラムをロード
    uint offset = pio_add_program(pio, &i2s_program);

    // ステートマシンを取得
    sm = pio_claim_unused_sm(pio, true);

    // ステートマシンを設定
    pio_sm_config c = pio_get_default_sm_config();

    // OUT ピンを設定（DATA）
    sm_config_set_out_pins(&c, I2S_DATA_PIN, 1);

    // サイドセットピンを設定（BCLK, LRCLK）
    sm_config_set_sideset_pins(&c, I2S_BCLK_PIN);

    // シフト方向（MSB first）
    sm_config_set_out_shift(&c, false, true, bits_per_sample);

    // クロック分周を設定
    // BCLK = sample_rate * bits_per_sample * 2 (LとR)
    uint32_t bclk_freq = sample_rate * bits_per_sample * 2;
    float div = (float)clock_get_hz(clk_sys) / (bclk_freq * 2);  // *2 は PIO の1サイクルで2クロック
    sm_config_set_clkdiv(&c, div);

    // ステートマシンを初期化
    pio_sm_init(pio, sm, offset, &c);

    // ステートマシンを有効化
    pio_sm_set_enabled(pio, sm, true);
}

// ============================================================================
// PCM データをバッファに書き込む
// ============================================================================

uint32_t audio_out_i2s_write(const int16_t *pcm_data, uint32_t num_samples) {
    uint32_t samples_written = 0;

    for (uint32_t i = 0; i < num_samples; i++) {
        uint32_t free_space = AUDIO_BUFFER_SIZE - buffered_samples;

        if (free_space == 0) {
            // バッファがいっぱい（オーバーラン）
            overrun_count++;
            break;
        }

        // ステレオデータを書き込む
        for (uint8_t ch = 0; ch < num_channels; ch++) {
            ring_buffer[write_pos * num_channels + ch] = pcm_data[i * num_channels + ch];
        }

        write_pos = (write_pos + 1) % AUDIO_BUFFER_SIZE;
        buffered_samples++;
        samples_written++;
    }

    return samples_written;
}

// ============================================================================
// バッファの空き容量を取得
// ============================================================================

uint32_t audio_out_i2s_get_free_space(void) {
    return AUDIO_BUFFER_SIZE - buffered_samples;
}

// ============================================================================
// バッファ内のデータ量を取得
// ============================================================================

uint32_t audio_out_i2s_get_buffered_samples(void) {
    return buffered_samples;
}

// ============================================================================
// オーディオ出力を開始
// ============================================================================

void audio_out_i2s_start(void) {
    if (is_running) return;

    printf("Starting I2S audio output...\n");

    // 最初の DMA バッファを埋める
    fill_dma_buffer(dma_buffer_0, DMA_BUFFER_SIZE);
    fill_dma_buffer(dma_buffer_1, DMA_BUFFER_SIZE);

    // DMA を開始
    dma_channel_start(dma_channel_0);

    is_running = true;
    printf("I2S audio output started\n");
}

// ============================================================================
// オーディオ出力を停止
// ============================================================================

void audio_out_i2s_stop(void) {
    if (!is_running) return;

    printf("Stopping I2S audio output...\n");

    // DMA を停止
    dma_channel_abort(dma_channel_0);
    dma_channel_abort(dma_channel_1);

    is_running = false;
    printf("I2S audio output stopped\n");
}

// ============================================================================
// バッファをクリア
// ============================================================================

void audio_out_i2s_clear_buffer(void) {
    write_pos = 0;
    read_pos = 0;
    buffered_samples = 0;
    underrun_count = 0;
    overrun_count = 0;

    memset(ring_buffer, 0, sizeof(ring_buffer));
    memset(dma_buffer_0, 0, sizeof(dma_buffer_0));
    memset(dma_buffer_1, 0, sizeof(dma_buffer_1));
}

// ============================================================================
// バッファ統計情報を取得
// ============================================================================

void audio_out_i2s_get_stats(uint32_t *underruns, uint32_t *overruns) {
    if (underruns) *underruns = underrun_count;
    if (overruns) *overruns = overrun_count;
}

// ============================================================================
// DMA バッファを埋める
// ============================================================================

static void fill_dma_buffer(int16_t *buffer, uint32_t num_samples) {
    for (uint32_t i = 0; i < num_samples; i++) {
        if (buffered_samples > 0) {
            // リングバッファからデータを読み出す
            for (uint8_t ch = 0; ch < num_channels; ch++) {
                buffer[i * num_channels + ch] = ring_buffer[read_pos * num_channels + ch];
            }

            read_pos = (read_pos + 1) % AUDIO_BUFFER_SIZE;
            buffered_samples--;
        } else {
            // データがない場合は無音を出力（アンダーラン）
            for (uint8_t ch = 0; ch < num_channels; ch++) {
                buffer[i * num_channels + ch] = 0;
            }
            underrun_count++;
        }
    }
}

// ============================================================================
// DMA 割り込みハンドラー
// ============================================================================

static void dma_handler(void) {
    // どちらの DMA チャンネルが完了したかを確認
    if (dma_channel_get_irq0_status(dma_channel_0)) {
        dma_channel_acknowledge_irq0(dma_channel_0);
        // チャンネル 0 が完了したので、バッファ 0 を再充填
        fill_dma_buffer(dma_buffer_0, DMA_BUFFER_SIZE);
    }

    if (dma_channel_get_irq0_status(dma_channel_1)) {
        dma_channel_acknowledge_irq0(dma_channel_1);
        // チャンネル 1 が完了したので、バッファ 1 を再充填
        fill_dma_buffer(dma_buffer_1, DMA_BUFFER_SIZE);
    }
}
