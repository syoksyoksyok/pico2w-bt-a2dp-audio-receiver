/**
 * @file audio_out_i2s.c
 * @brief I2S DAC オーディオ出力モジュール（PIO + DMA実装）
 *
 * PCM5102などのI2S DACモジュールに対応
 * PIOを使用してI2Sプロトコルを実装し、DMAで効率的にデータ転送
 */

#include "audio_out_i2s.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#include "i2s.pio.h"

// ============================================================================
// 内部変数
// ============================================================================

static PIO pio = pio0;
static uint sm = 0;
static int dma_channel = -1;

static uint32_t sample_rate_hz = 44100;
static uint8_t bits_per_sample = 16;
static uint8_t num_channels = 2;

// リングバッファ（ステレオ 16bit）
#define I2S_BUFFER_SIZE (AUDIO_BUFFER_SIZE * 2)  // ステレオなので2倍
static int16_t ring_buffer[I2S_BUFFER_SIZE];
static volatile uint32_t write_pos = 0;
static volatile uint32_t read_pos = 0;
static volatile uint32_t buffered_samples = 0;  // ステレオフレーム数

// DMA バッファ（32bit words for PIO）
#define I2S_DMA_BUFFER_SIZE 512
static uint32_t dma_buffer[I2S_DMA_BUFFER_SIZE];

// 統計情報
static uint32_t underrun_count = 0;
static uint32_t overrun_count = 0;

// 状態
static bool is_running = false;

// ============================================================================
// 内部関数（前方宣言）
// ============================================================================

static void dma_handler(void);
static void fill_dma_buffer(void);

// ============================================================================
// 初期化
// ============================================================================

bool audio_out_i2s_init(uint32_t sample_rate, uint8_t bits, uint8_t channels) {
    printf("Initializing I2S audio output...\n");
    printf("  Sample rate: %lu Hz\n", sample_rate);
    printf("  Bits per sample: %u\n", bits);
    printf("  Channels: %u\n", channels);
    printf("  I2S pins: DATA=%d, BCLK=%d, LRCLK=%d\n",
           I2S_DATA_PIN, I2S_BCLK_PIN, I2S_LRCLK_PIN);

    sample_rate_hz = sample_rate;
    bits_per_sample = bits;
    num_channels = channels;

    // リングバッファの初期化
    memset(ring_buffer, 0, sizeof(ring_buffer));
    write_pos = 0;
    read_pos = 0;
    buffered_samples = 0;

    // PIO プログラムをロード
    uint offset = pio_add_program(pio, &i2s_output_program);

    // PIO 初期化
    i2s_output_program_init(pio, sm, offset, I2S_DATA_PIN, I2S_BCLK_PIN, I2S_LRCLK_PIN);

    // PIO クロック設定
    // BCLK frequency = sample_rate × 64 (32 bits per channel × 2 channels)
    // PIOプログラムは1ビットあたり2命令使用するため、PIO clock = BCLK × 2
    // 44100 Hz → BCLK = 2.8224 MHz, PIO clock = 5.6448 MHz
    uint32_t bclk_freq = sample_rate * 64;
    uint32_t pio_freq = bclk_freq * 2;  // 2 PIO cycles per bit
    float div = (float)clock_get_hz(clk_sys) / pio_freq;
    pio_sm_set_clkdiv(pio, sm, div);

    printf("  BCLK frequency: %lu Hz, PIO clock: %lu Hz (divider: %.2f)\n",
           bclk_freq, pio_freq, div);

    // DMA チャンネルを取得
    dma_channel = dma_claim_unused_channel(true);

    // DMA 設定
    dma_channel_config dma_config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_dreq(&dma_config, pio_get_dreq(pio, sm, true));

    dma_channel_configure(
        dma_channel,
        &dma_config,
        &pio->txf[sm],           // Write address (PIO TX FIFO)
        dma_buffer,              // Read address (DMA buffer)
        I2S_DMA_BUFFER_SIZE,     // Transfer count
        false                    // Don't start yet
    );

    // DMA 割り込みハンドラを設定
    dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    printf("I2S audio output initialized successfully\n");
    printf("  DMA channel: %d\n", dma_channel);
    printf("  PIO: %s, SM: %u\n", pio == pio0 ? "pio0" : "pio1", sm);

    return true;
}

// ============================================================================
// 開始・停止
// ============================================================================

void audio_out_i2s_start(void) {
    if (is_running) return;

    printf("Starting I2S audio output...\n");

    // 初回DMAバッファを埋める
    fill_dma_buffer();

    // DMA 開始
    dma_channel_start(dma_channel);

    is_running = true;
    printf("I2S audio output started\n");
}

void audio_out_i2s_stop(void) {
    if (!is_running) return;

    printf("Stopping I2S audio output...\n");

    // DMA 停止
    dma_channel_abort(dma_channel);

    is_running = false;
    printf("I2S audio output stopped\n");
}

// ============================================================================
// PCMデータ書き込み
// ============================================================================

uint32_t audio_out_i2s_write(const int16_t *pcm_data, uint32_t num_samples) {
    if (!is_running) return 0;

    uint32_t written = 0;
    uint32_t buffer_capacity = I2S_BUFFER_SIZE / 2;  // ステレオフレーム数

    // クリティカルセクション
    uint32_t irq_status = save_and_disable_interrupts();

    for (uint32_t i = 0; i < num_samples; i++) {
        // バッファフル確認
        if (buffered_samples >= buffer_capacity) {
            overrun_count++;
            break;
        }

        // ステレオデータを書き込み
        ring_buffer[write_pos * 2] = pcm_data[i * 2];          // Left
        ring_buffer[write_pos * 2 + 1] = pcm_data[i * 2 + 1];  // Right

        write_pos = (write_pos + 1) % buffer_capacity;
        buffered_samples++;
        written++;
    }

    restore_interrupts(irq_status);

    return written;
}

// ============================================================================
// バッファ情報取得
// ============================================================================

uint32_t audio_out_i2s_get_free_space(void) {
    uint32_t buffer_capacity = I2S_BUFFER_SIZE / 2;
    uint32_t irq_status = save_and_disable_interrupts();
    uint32_t free = buffer_capacity - buffered_samples;
    restore_interrupts(irq_status);
    return free;
}

uint32_t audio_out_i2s_get_buffered_samples(void) {
    uint32_t irq_status = save_and_disable_interrupts();
    uint32_t buffered = buffered_samples;
    restore_interrupts(irq_status);
    return buffered;
}

void audio_out_i2s_clear_buffer(void) {
    uint32_t irq_status = save_and_disable_interrupts();
    write_pos = 0;
    read_pos = 0;
    buffered_samples = 0;
    memset(ring_buffer, 0, sizeof(ring_buffer));
    restore_interrupts(irq_status);
    printf("I2S buffer cleared\n");
}

void audio_out_i2s_get_stats(uint32_t *underruns, uint32_t *overruns) {
    if (underruns) *underruns = underrun_count;
    if (overruns) *overruns = overrun_count;
}

// ============================================================================
// 内部関数: DMAバッファを埋める
// ============================================================================

static void fill_dma_buffer(void) {
    uint32_t buffer_capacity = I2S_BUFFER_SIZE / 2;

    for (uint32_t i = 0; i < I2S_DMA_BUFFER_SIZE; i++) {
        int16_t left, right;

        if (buffered_samples > 0) {
            // リングバッファからデータを読み取り
            left = ring_buffer[read_pos * 2];
            right = ring_buffer[read_pos * 2 + 1];

            read_pos = (read_pos + 1) % buffer_capacity;
            buffered_samples--;
        } else {
            // バッファアンダーラン - サイレンス出力
            left = 0;
            right = 0;
            underrun_count++;
        }

        // 32bitワードにパック (MSB first)
        // I2S format: bits 31-16 = left channel, bits 15-0 = right channel
        // 符号拡張を防ぐため、明示的にuint16_tにキャスト
        dma_buffer[i] = ((uint32_t)(uint16_t)left << 16) | (uint32_t)(uint16_t)right;
    }
}

// ============================================================================
// DMA割り込みハンドラ
// ============================================================================

static void dma_handler(void) {
    // DMA割り込みフラグをクリア
    dma_hw->ints0 = 1u << dma_channel;

    if (!is_running) return;

    // 次のDMAバッファを準備
    fill_dma_buffer();

    // DMA再開
    dma_channel_set_read_addr(dma_channel, dma_buffer, true);
}
