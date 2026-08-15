/**
 * @file audio_out_i2s.c
 * @brief I2S DAC オーディオ出力モジュール（PIO + DMA実装）
 *
 * PCM5102A DAC用のI2S出力を、Raspberry Pi PicoのPIOとDMAを使用して実装
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

// PIOプログラムのインクルード（ビルド時に自動生成される）
#include "i2s.pio.h"

// ============================================================================
// 内部変数
// ============================================================================

static PIO pio = pio0;
static uint sm = 0;
static uint offset;
static int dma_channel = -1;

static uint32_t sample_rate_hz = 44100;
static uint8_t bits_per_sample = 16;
static uint8_t num_channels = 2;

// リングバッファ（ステレオ16ビット）
#define I2S_BUFFER_SIZE (AUDIO_BUFFER_SIZE * 2)  // ステレオなので2倍
static int16_t ring_buffer[I2S_BUFFER_SIZE];
static volatile uint32_t write_pos = 0;
static volatile uint32_t read_pos = 0;
static volatile uint32_t buffered_samples = 0;  // ステレオペア数

// DMA バッファ（2つのバッファでピンポン方式）
// バッファサイズを512サンプル（約11.6ms@44.1kHz）に増加
// これにより、DMA IRQ頻度が大幅に減少し、ジッター/ノイズが低減される
// DMA IRQ優先度を0xFF（最低）に設定済みなので、Bluetooth処理を妨害しない
#define I2S_DMA_BUFFER_SIZE DMA_BUFFER_SIZE
static int32_t dma_buffer[2][I2S_DMA_BUFFER_SIZE];  // 32ビット（左右16ビットずつ）
static int32_t silence_dma_buffer[I2S_DMA_BUFFER_SIZE];

typedef enum {
    DMA_BUFFER_EMPTY = 0,
    DMA_BUFFER_READY,
    DMA_BUFFER_IN_DMA
} dma_buffer_state_t;

static volatile dma_buffer_state_t dma_buffer_state[2] = {DMA_BUFFER_EMPTY, DMA_BUFFER_EMPTY};
static volatile uint8_t current_dma_buffer = 0xffu;
static volatile uint8_t dma_refill_mask = 0;

// 統計情報
static volatile uint32_t underrun_count = 0;
static volatile uint32_t overrun_count = 0;
static volatile bool rebuffering = false;

// 状態
static volatile bool is_running = false;

#if ENABLE_DEBUG_LOG
static volatile uint32_t min_buffered_samples = UINT32_MAX;
static volatile uint32_t max_buffered_samples = 0;
static volatile uint32_t dropped_stereo_samples = 0;
static volatile uint32_t rebuffer_count = 0;
static volatile uint32_t dma_switch_count = 0;
#endif

// ============================================================================
// 内部関数（前方宣言）
// ============================================================================

static void dma_handler(void);
static void fill_dma_buffer(int32_t *buffer, uint32_t num_samples);
static void mark_buffer_level_locked(uint32_t buffered);
static void enter_rebuffering_locked(void);

static inline uint32_t ring_buffer_capacity(void) {
    return I2S_BUFFER_SIZE / 2;
}

static void mark_buffer_level_locked(uint32_t buffered) {
#if ENABLE_DEBUG_LOG
    if (buffered < min_buffered_samples) {
        min_buffered_samples = buffered;
    }
    if (buffered > max_buffered_samples) {
        max_buffered_samples = buffered;
    }
#else
    (void)buffered;
#endif
}

static void enter_rebuffering_locked(void) {
    if (!rebuffering) {
        rebuffering = true;
#if ENABLE_DEBUG_LOG
        rebuffer_count++;
#endif
    }
}

// ============================================================================
// I2S オーディオ出力の初期化
// ============================================================================

bool audio_out_i2s_init(uint32_t sample_rate, uint8_t bits, uint8_t channels) {
    printf("Initializing I2S audio output (PIO-based)...\n");
    printf("  Sample rate: %lu Hz\n", sample_rate);
    printf("  Bits per sample: %d\n", bits);
    printf("  Channels: %d\n", channels);
    printf("  I2S pins: DATA=%d, BCLK=%d, LRCLK=%d\n",
           I2S_DATA_PIN, I2S_BCLK_PIN, I2S_LRCLK_PIN);

    sample_rate_hz = sample_rate;
    bits_per_sample = bits;
    num_channels = channels;

    // PIOプログラムをロード
    offset = pio_add_program(pio, &i2s_output_program);
    printf("  PIO program loaded at offset %d\n", offset);

    // PIOクロック設定の計算と表示
    uint32_t pio_clk_freq = sample_rate * PIO_CYCLES_PER_STEREO_SAMPLE;
    uint32_t sys_clk = clock_get_hz(clk_sys);
    float clk_div = (float)sys_clk / (float)pio_clk_freq;
    printf("  PIO clock: %lu Hz (divider: %.2f)\n", pio_clk_freq, clk_div);
    printf("  BCLK frequency: %lu Hz (generated)\n", pio_clk_freq / 2);

    // PIO State Machineを初期化
    i2s_output_program_init(pio, sm, offset, I2S_DATA_PIN, I2S_BCLK_PIN, sample_rate);
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);

    // DMA チャンネルを取得
    dma_channel = dma_claim_unused_channel(true);
    printf("I2S audio output initialized successfully\n");
    printf("  DMA channel: %d\n", dma_channel);
    printf("  PIO: pio%d, SM: %d\n", pio == pio0 ? 0 : 1, sm);

    // DMA の設定
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);  // 32ビット転送
    channel_config_set_read_increment(&c, true);              // 読み込みアドレス増加
    channel_config_set_write_increment(&c, false);            // 書き込みアドレス固定
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true)); // PIO TX FIFOをトリガー

    dma_channel_configure(
        dma_channel,
        &c,
        &pio->txf[sm],              // 書き込み先: PIO TX FIFO
        dma_buffer[0],              // 読み込み元: DMAバッファ
        I2S_DMA_BUFFER_SIZE,        // 転送数
        false                       // まだ開始しない
    );

    // DMA 割り込みハンドラーを設定
    dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);

    // DMA割り込み優先度を絶対最低に設定（CYW43のBluetooth処理を妨害しないため）
    // 0x00=最高優先度、0xFF=絶対最低優先度
    // Cortex-M33では実際には2ビット優先度（0-3）で、0xFFは最低の3にマップされる
    irq_set_priority(DMA_IRQ_0, DMA_IRQ_PRIORITY);

    irq_set_enabled(DMA_IRQ_0, true);
    printf("  DMA IRQ priority set to absolute lowest (0x%02X)\n", DMA_IRQ_PRIORITY);

    // バッファをクリア
    audio_out_i2s_clear_buffer();

    return true;
}

// ============================================================================
// PCM データをバッファに書き込む
// ============================================================================

uint32_t audio_out_i2s_write(const int16_t *pcm_data, uint32_t num_samples) {
    uint32_t samples_written = 0;

    // num_samplesはステレオペア数として扱う
    while (samples_written < num_samples) {
        uint32_t save = save_and_disable_interrupts();
        uint32_t free_samples = ring_buffer_capacity() - buffered_samples;

        if (free_samples == 0) {
            overrun_count++;
#if ENABLE_DEBUG_LOG
            dropped_stereo_samples += (num_samples - samples_written);
#endif
            restore_interrupts(save);
            break;
        }

        uint32_t contiguous = ring_buffer_capacity() - write_pos;
        uint32_t chunk = num_samples - samples_written;
        if (chunk > free_samples) {
            chunk = free_samples;
        }
        if (chunk > contiguous) {
            chunk = contiguous;
        }
        if (chunk > 64u) {
            chunk = 64u;
        }

        memcpy(&ring_buffer[write_pos * 2u],
               &pcm_data[samples_written * 2u],
               chunk * 2u * sizeof(int16_t));

        write_pos = (write_pos + chunk) % ring_buffer_capacity();
        buffered_samples += chunk;
        mark_buffer_level_locked(buffered_samples);
        restore_interrupts(save);

        samples_written += chunk;
    }

    return samples_written;
}

// ============================================================================
// バッファの空き容量を取得
// ============================================================================

uint32_t audio_out_i2s_get_free_space(void) {
    uint32_t save = save_and_disable_interrupts();
    uint32_t buffered = buffered_samples;
    restore_interrupts(save);
    return ring_buffer_capacity() - buffered;
}

// ============================================================================
// バッファ内のデータ量を取得
// ============================================================================

uint32_t audio_out_i2s_get_buffered_samples(void) {
    uint32_t save = save_and_disable_interrupts();
    uint32_t buffered = buffered_samples;
    restore_interrupts(save);
    return buffered;
}

// ============================================================================
// 割り込み外で行うI2S処理
// ============================================================================

void audio_out_i2s_process(void) {
    uint32_t save = save_and_disable_interrupts();
    bool should_start = !is_running && buffered_samples >= AUTO_START_THRESHOLD;
    uint8_t refill_mask = dma_refill_mask;
    dma_refill_mask = 0;
    restore_interrupts(save);

    if (should_start) {
        audio_out_i2s_start();
        return;
    }

    for (uint8_t i = 0; i < 2u; i++) {
        if ((refill_mask & (1u << i)) == 0) {
            continue;
        }

        fill_dma_buffer(dma_buffer[i], I2S_DMA_BUFFER_SIZE);

        save = save_and_disable_interrupts();
        if (dma_buffer_state[i] == DMA_BUFFER_EMPTY) {
            dma_buffer_state[i] = DMA_BUFFER_READY;
        }
        restore_interrupts(save);
    }
}

// ============================================================================
// オーディオ出力を開始
// ============================================================================

void audio_out_i2s_start(void) {
    if (is_running) return;

    printf("Starting I2S audio output...\n");

    // ピンポンバッファ: 両方のバッファを事前に埋める（これが重要！）
    fill_dma_buffer(dma_buffer[0], I2S_DMA_BUFFER_SIZE);
    fill_dma_buffer(dma_buffer[1], I2S_DMA_BUFFER_SIZE);
    dma_buffer_state[0] = DMA_BUFFER_IN_DMA;
    dma_buffer_state[1] = DMA_BUFFER_READY;
    current_dma_buffer = 0;
    printf("  Both DMA buffers pre-filled\n");

    // PIO State Machine を有効化
    pio_sm_set_enabled(pio, sm, true);
    printf("  PIO SM enabled\n");

    // DMA を開始（buffer[0]から）
    dma_channel_set_read_addr(dma_channel, dma_buffer[0], false);
    dma_channel_set_trans_count(dma_channel, I2S_DMA_BUFFER_SIZE, false);
    dma_channel_acknowledge_irq0(dma_channel);
    dma_channel_start(dma_channel);
    printf("  DMA started\n");

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
    dma_channel_abort(dma_channel);

    // PIO State Machineを停止
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);

    dma_channel_acknowledge_irq0(dma_channel);

    dma_buffer_state[0] = DMA_BUFFER_EMPTY;
    dma_buffer_state[1] = DMA_BUFFER_EMPTY;
    dma_refill_mask = 0;
    current_dma_buffer = 0xffu;

    is_running = false;
    printf("I2S audio output stopped\n");
}

// ============================================================================
// バッファをクリア
// ============================================================================

void audio_out_i2s_clear_buffer(void) {
    if (is_running) {
        audio_out_i2s_stop();
    }

    uint32_t save = save_and_disable_interrupts();
    write_pos = 0;
    read_pos = 0;
    buffered_samples = 0;
    underrun_count = 0;
    overrun_count = 0;
    rebuffering = false;
    dma_buffer_state[0] = DMA_BUFFER_EMPTY;
    dma_buffer_state[1] = DMA_BUFFER_EMPTY;
    dma_refill_mask = 0;
    current_dma_buffer = 0xffu;
#if ENABLE_DEBUG_LOG
    min_buffered_samples = UINT32_MAX;
    max_buffered_samples = 0;
    dropped_stereo_samples = 0;
    rebuffer_count = 0;
    dma_switch_count = 0;
#endif
    restore_interrupts(save);

    // 無音で埋める
    memset(ring_buffer, 0, sizeof(ring_buffer));
    memset(dma_buffer, 0, sizeof(dma_buffer));
}

// ============================================================================
// バッファ統計情報を取得
// ============================================================================

void audio_out_i2s_get_stats(uint32_t *underruns, uint32_t *overruns) {
    uint32_t save = save_and_disable_interrupts();
    uint32_t underrun_snapshot = underrun_count;
    uint32_t overrun_snapshot = overrun_count;
    restore_interrupts(save);

    if (underruns) *underruns = underrun_snapshot;
    if (overruns) *overruns = overrun_snapshot;
}

#if ENABLE_DEBUG_LOG
void audio_out_i2s_get_debug_stats(audio_out_i2s_debug_stats_t *stats) {
    if (!stats) {
        return;
    }

    uint32_t save = save_and_disable_interrupts();
    stats->buffered_samples = buffered_samples;
    stats->free_samples = ring_buffer_capacity() - buffered_samples;
    stats->min_buffered_samples = (min_buffered_samples == UINT32_MAX) ? buffered_samples : min_buffered_samples;
    stats->max_buffered_samples = max_buffered_samples;
    stats->underruns = underrun_count;
    stats->overruns = overrun_count;
    stats->dropped_samples = dropped_stereo_samples;
    stats->rebuffer_count = rebuffer_count;
    stats->dma_switch_count = dma_switch_count;
    stats->dma_buffer_state[0] = (uint8_t)dma_buffer_state[0];
    stats->dma_buffer_state[1] = (uint8_t)dma_buffer_state[1];
    stats->rebuffering = rebuffering;
    stats->running = is_running;
    restore_interrupts(save);
}
#endif

// ============================================================================
// DMA バッファを埋める
// ============================================================================

static void fill_dma_buffer(int32_t *buffer, uint32_t num_samples) {
    uint32_t filled = 0;

    while (filled < num_samples) {
        uint32_t save = save_and_disable_interrupts();

        if (rebuffering) {
            if (buffered_samples < REBUFFER_THRESHOLD) {
                restore_interrupts(save);
                memset(&buffer[filled], 0, (num_samples - filled) * sizeof(int32_t));
                return;
            }
            rebuffering = false;
        }

        if (buffered_samples == 0) {
            underrun_count++;
            enter_rebuffering_locked();
            restore_interrupts(save);
            memset(&buffer[filled], 0, (num_samples - filled) * sizeof(int32_t));
            return;
        }

        uint32_t contiguous = ring_buffer_capacity() - read_pos;
        uint32_t chunk = num_samples - filled;
        if (chunk > buffered_samples) {
            chunk = buffered_samples;
        }
        if (chunk > contiguous) {
            chunk = contiguous;
        }
        if (chunk > 64u) {
            chunk = 64u;
        }

        for (uint32_t i = 0; i < chunk; i++) {
            uint32_t src = (read_pos + i) * 2u;
            int16_t left = ring_buffer[src];
            int16_t right = ring_buffer[src + 1u];
            buffer[filled + i] = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
        }

        read_pos = (read_pos + chunk) % ring_buffer_capacity();
        buffered_samples -= chunk;
        mark_buffer_level_locked(buffered_samples);
        restore_interrupts(save);

        filled += chunk;
    }
}

// ============================================================================
// DMA 割り込みハンドラー
// ============================================================================

static void dma_handler(void) {
    if (dma_channel_get_irq0_status(dma_channel)) {
        dma_channel_acknowledge_irq0(dma_channel);

        if (current_dma_buffer < 2u) {
            dma_buffer_state[current_dma_buffer] = DMA_BUFFER_EMPTY;
            dma_refill_mask |= (uint8_t)(1u << current_dma_buffer);
        }

        uint8_t next_buffer = 0xffu;
        if (current_dma_buffer < 2u) {
            uint8_t preferred = 1u - current_dma_buffer;
            if (dma_buffer_state[preferred] == DMA_BUFFER_READY) {
                next_buffer = preferred;
            }
        }
        if (next_buffer == 0xffu) {
            for (uint8_t i = 0; i < 2u; i++) {
                if (dma_buffer_state[i] == DMA_BUFFER_READY) {
                    next_buffer = i;
                    break;
                }
            }
        }

        const int32_t *next_data = silence_dma_buffer;
        if (next_buffer < 2u) {
            dma_buffer_state[next_buffer] = DMA_BUFFER_IN_DMA;
            next_data = dma_buffer[next_buffer];
            current_dma_buffer = next_buffer;
        } else {
            underrun_count++;
            enter_rebuffering_locked();
            current_dma_buffer = 0xffu;
        }

        // DMAを次の準備済みバッファ、または無音バッファで即座に再起動
        dma_channel_set_trans_count(dma_channel, I2S_DMA_BUFFER_SIZE, false);
        dma_channel_set_read_addr(dma_channel, next_data, true);
#if ENABLE_DEBUG_LOG
        dma_switch_count++;
#endif
    }
}
