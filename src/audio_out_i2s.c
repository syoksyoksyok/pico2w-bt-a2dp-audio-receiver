/**
 * @file audio_out_i2s.c
 * @brief I2S DAC オーディオ出力モジュール（PIO実装）
 *
 * PCM5102等のI2S DACモジュールに対応したオーディオ出力実装。
 * PIO (Programmable I/O) を使用してI2S信号を生成。
 */

#include "audio_out_i2s.h"
#include "config.h"
#include "audio_i2s.pio.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"

// ============================================================================
// グローバル変数
// ============================================================================

static PIO pio = pio0;
static uint sm = 0;
static int dma_chan = -1;

// リングバッファ
static int16_t audio_buffer[AUDIO_BUFFER_SIZE * 2];  // ステレオ
static volatile uint32_t write_pos = 0;
static volatile uint32_t read_pos = 0;
static volatile uint32_t buffered_samples = 0;

// DMAバッファ（2つのバッファをピンポンで使用）
static int16_t dma_buffers[2][DMA_BUFFER_SIZE * 2];  // ステレオ
static volatile uint8_t current_dma_buffer = 0;

// 統計情報
static volatile uint32_t underrun_count = 0;
static volatile uint32_t overrun_count = 0;
static volatile bool is_playing = false;

// サンプルレート
static uint32_t current_sample_rate = 0;

// ============================================================================
// DMA割り込みハンドラ
// ============================================================================

static void dma_irq_handler(void) {
    if (dma_channel_get_irq0_status(dma_chan)) {
        dma_channel_acknowledge_irq0(dma_chan);

        // 次のバッファを切り替え
        current_dma_buffer = 1 - current_dma_buffer;
        uint8_t next_buffer = current_dma_buffer;

        // 次のDMA転送を設定
        dma_channel_set_read_addr(dma_chan, dma_buffers[next_buffer], false);

        // リングバッファから次のDMAバッファにデータをコピー
        uint32_t samples_to_copy = DMA_BUFFER_SIZE;

        if (buffered_samples >= samples_to_copy) {
            for (uint32_t i = 0; i < samples_to_copy; i++) {
                dma_buffers[next_buffer][i * 2] = audio_buffer[read_pos * 2];         // Left
                dma_buffers[next_buffer][i * 2 + 1] = audio_buffer[read_pos * 2 + 1]; // Right
                read_pos = (read_pos + 1) % AUDIO_BUFFER_SIZE;
            }
            buffered_samples -= samples_to_copy;
        } else {
            // アンダーラン：無音データを送信
            memset(dma_buffers[next_buffer], 0, DMA_BUFFER_SIZE * 2 * sizeof(int16_t));
            underrun_count++;
        }
    }
}

// ============================================================================
// 初期化関数
// ============================================================================

bool audio_out_i2s_init(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels) {
    printf("[I2S] Starting initialization...\n");
    fflush(stdout);

    if (bits_per_sample != 16 || channels != 2) {
        printf("ERROR: I2S only supports 16-bit stereo audio\n");
        fflush(stdout);
        return false;
    }

    current_sample_rate = sample_rate;
    printf("[I2S] Sample rate: %lu Hz\n", sample_rate);
    fflush(stdout);

    // バッファ初期化
    printf("[I2S] Initializing buffers...\n");
    fflush(stdout);
    memset((void *)audio_buffer, 0, sizeof(audio_buffer));
    memset((void *)dma_buffers, 0, sizeof(dma_buffers));
    write_pos = 0;
    read_pos = 0;
    buffered_samples = 0;
    current_dma_buffer = 0;
    underrun_count = 0;
    overrun_count = 0;
    is_playing = false;

    // PIOプログラムをロード
    printf("[I2S] Loading PIO program...\n");
    fflush(stdout);
    uint offset = pio_add_program(pio, &audio_i2s_program);
    printf("[I2S] PIO program loaded at offset %u\n", offset);
    fflush(stdout);

    // ステートマシン取得
    printf("[I2S] Claiming PIO state machine...\n");
    fflush(stdout);
    sm = pio_claim_unused_sm(pio, true);
    printf("[I2S] Claimed state machine %u\n", sm);
    fflush(stdout);

    // PIOピン設定
    printf("[I2S] Configuring GPIO pins...\n");
    fflush(stdout);
    // OUT pin: DATA
    pio_gpio_init(pio, I2S_DATA_PIN);
    pio_sm_set_consecutive_pindirs(pio, sm, I2S_DATA_PIN, 1, true);

    // Side-set pins: LRCLK, BCLK
    pio_gpio_init(pio, I2S_LRCLK_PIN);
    pio_gpio_init(pio, I2S_BCLK_PIN);
    pio_sm_set_consecutive_pindirs(pio, sm, I2S_LRCLK_PIN, 2, true);
    printf("[I2S] GPIO pins configured\n");
    fflush(stdout);

    // ステートマシン設定
    printf("[I2S] Configuring state machine...\n");
    fflush(stdout);
    pio_sm_config sm_config = audio_i2s_program_get_default_config(offset);

    // OUT pins設定（DATA）
    sm_config_set_out_pins(&sm_config, I2S_DATA_PIN, 1);

    // Side-set pins設定（LRCLK, BCLK）
    sm_config_set_sideset_pins(&sm_config, I2S_LRCLK_PIN);

    // シフト設定：左シフト、自動pull、16ビット
    sm_config_set_out_shift(&sm_config, false, true, 16);

    // FIFO結合：TXのみ使用
    sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX);

    // クロック分周設定
    // BCLK frequency = sample_rate * bits_per_sample * channels
    // 44100 Hz * 16 bits * 2 channels = 1,411,200 Hz
    // PIO runs at BCLK * 2 (for rising and falling edges)
    float bclk_freq = sample_rate * bits_per_sample * channels;
    float pio_freq = bclk_freq * 2;  // PIO needs to run at 2x BCLK
    float div = clock_get_hz(clk_sys) / pio_freq;
    sm_config_set_clkdiv(&sm_config, div);

    // ステートマシン初期化
    printf("[I2S] Initializing state machine...\n");
    fflush(stdout);
    pio_sm_init(pio, sm, offset, &sm_config);
    printf("[I2S] State machine initialized\n");
    fflush(stdout);

    printf("I2S initialized: %lu Hz, %u-bit, %u-ch\n", sample_rate, bits_per_sample, channels);
    printf("BCLK: %.0f Hz, PIO clock div: %.2f\n", bclk_freq, div);
    fflush(stdout);

    // DMAチャネル取得
    printf("[I2S] Claiming DMA channel...\n");
    fflush(stdout);
    dma_chan = dma_claim_unused_channel(true);
    printf("[I2S] DMA channel %d claimed\n", dma_chan);
    fflush(stdout);

    // DMA設定
    printf("[I2S] Configuring DMA...\n");
    fflush(stdout);
    dma_channel_config dma_config = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_dreq(&dma_config, pio_get_dreq(pio, sm, true));

    // DMA転送設定
    printf("[I2S] Configuring DMA transfer...\n");
    fflush(stdout);
    dma_channel_configure(
        dma_chan,
        &dma_config,
        &pio->txf[sm],                    // Write address: PIO TX FIFO
        dma_buffers[0],                   // Read address: DMA buffer
        DMA_BUFFER_SIZE * 2,              // Transfer count (stereo samples)
        false                             // Don't start yet
    );

    // DMA割り込み設定
    printf("[I2S] Setting up DMA interrupt...\n");
    fflush(stdout);
    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    printf("[I2S] DMA interrupt configured\n");
    fflush(stdout);

    printf("I2S output initialized successfully\n");
    fflush(stdout);
    printf("Pin configuration:\n");
    printf("  DATA (DIN):  GPIO %d\n", I2S_DATA_PIN);
    printf("  BCLK (BCK):  GPIO %d\n", I2S_BCLK_PIN);
    printf("  LRCLK (LCK): GPIO %d\n", I2S_LRCLK_PIN);

    return true;
}

// ============================================================================
// オーディオ出力関数
// ============================================================================

uint32_t audio_out_i2s_write(const int16_t *pcm_data, uint32_t num_samples) {
    if (!pcm_data || num_samples == 0) {
        return 0;
    }

    uint32_t samples_written = 0;
    uint32_t free_space = AUDIO_BUFFER_SIZE - buffered_samples;

    if (num_samples > free_space) {
        // オーバーラン
        num_samples = free_space;
        overrun_count++;
    }

    // リングバッファに書き込み（ステレオ）
    for (uint32_t i = 0; i < num_samples; i++) {
        audio_buffer[write_pos * 2] = pcm_data[i * 2];         // Left
        audio_buffer[write_pos * 2 + 1] = pcm_data[i * 2 + 1]; // Right
        write_pos = (write_pos + 1) % AUDIO_BUFFER_SIZE;
        samples_written++;
    }

    buffered_samples += samples_written;

    return samples_written;
}

uint32_t audio_out_i2s_get_free_space(void) {
    return AUDIO_BUFFER_SIZE - buffered_samples;
}

uint32_t audio_out_i2s_get_buffered_samples(void) {
    return buffered_samples;
}

void audio_out_i2s_start(void) {
    if (is_playing) {
        return;
    }

    // 最初のDMAバッファを準備
    uint32_t samples_to_copy = DMA_BUFFER_SIZE;
    if (buffered_samples >= samples_to_copy) {
        for (uint32_t i = 0; i < samples_to_copy; i++) {
            dma_buffers[0][i * 2] = audio_buffer[read_pos * 2];         // Left
            dma_buffers[0][i * 2 + 1] = audio_buffer[read_pos * 2 + 1]; // Right
            read_pos = (read_pos + 1) % AUDIO_BUFFER_SIZE;
        }
        buffered_samples -= samples_to_copy;
    } else {
        memset(dma_buffers[0], 0, DMA_BUFFER_SIZE * 2 * sizeof(int16_t));
    }

    // PIO開始
    pio_sm_set_enabled(pio, sm, true);

    // DMA開始
    dma_channel_start(dma_chan);

    is_playing = true;
    printf("I2S playback started\n");
}

void audio_out_i2s_stop(void) {
    if (!is_playing) {
        return;
    }

    // DMA停止
    dma_channel_abort(dma_chan);

    // PIO停止
    pio_sm_set_enabled(pio, sm, false);

    is_playing = false;
    printf("I2S playback stopped\n");
}

void audio_out_i2s_clear_buffer(void) {
    audio_out_i2s_stop();

    memset((void *)audio_buffer, 0, sizeof(audio_buffer));
    write_pos = 0;
    read_pos = 0;
    buffered_samples = 0;

    printf("I2S buffer cleared\n");
}

void audio_out_i2s_get_stats(uint32_t *underruns, uint32_t *overruns) {
    if (underruns) *underruns = underrun_count;
    if (overruns) *overruns = overrun_count;
}
