/**
 * @file audio_out_i2s.c
 * @brief I2S DAC オーディオ出力モジュール（PIO + DMA実装）
 *
 * PCM5102 DAC用のI2S出力を、Raspberry Pi PicoのPIOとDMAを使用して実装
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
// 注意: 大きすぎるとDMA割り込みが重くなり、CYW43のBluetooth処理を妨害して切断される
// 128サンプル = 約2.9ms@44.1kHz（安全な範囲）
#define I2S_DMA_BUFFER_SIZE 128
static int32_t dma_buffer[2][I2S_DMA_BUFFER_SIZE];  // 32ビット（左右16ビットずつ）
static volatile uint8_t current_dma_buffer = 0;

// 統計情報
static uint32_t underrun_count = 0;
static uint32_t overrun_count = 0;

// 状態
static bool is_running = false;

// ============================================================================
// 内部関数（前方宣言）
// ============================================================================

static void dma_handler(void);
static void fill_dma_buffer(int32_t *buffer, uint32_t num_samples);

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

    // BCLK周波数の計算と表示
    uint32_t bclk_freq = sample_rate * 64;  // 16ビット × 2チャンネル × 2
    float clk_div = (float)clock_get_hz(clk_sys) / (bclk_freq * 2.0f);
    printf("  BCLK frequency: %lu Hz (divider: %.2f)\n", bclk_freq, clk_div);

    // PIO State Machineを初期化
    i2s_output_program_init(pio, sm, offset, I2S_DATA_PIN, I2S_BCLK_PIN, sample_rate);

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
    irq_set_priority(DMA_IRQ_0, 0xFF);

    irq_set_enabled(DMA_IRQ_0, true);
    printf("  DMA IRQ priority set to absolute lowest (0xFF)\n");

    // バッファをクリア
    audio_out_i2s_clear_buffer();

    return true;
}

// ============================================================================
// PCM データをバッファに書き込む
// ============================================================================

uint32_t audio_out_i2s_write(const int16_t *pcm_data, uint32_t num_samples) {
    uint32_t samples_written = 0;

    static uint32_t write_call_count = 0;
    static uint32_t total_written = 0;
    uint32_t buffered_before = buffered_samples;

    write_call_count++;

    // num_samplesはステレオペア数として扱う
    for (uint32_t i = 0; i < num_samples; i++) {
        uint32_t free_space = I2S_BUFFER_SIZE / 2 - buffered_samples;

        if (free_space == 0) {
            // バッファがいっぱい（オーバーラン）
            overrun_count++;
            break;
        }

        // ステレオデータをリングバッファに書き込み
        ring_buffer[write_pos * 2] = pcm_data[i * 2];       // 左チャンネル
        ring_buffer[write_pos * 2 + 1] = pcm_data[i * 2 + 1]; // 右チャンネル

        write_pos = (write_pos + 1) % (I2S_BUFFER_SIZE / 2);
        buffered_samples++;
        samples_written++;
    }

    total_written += samples_written;

    // 100回ごとにログ出力
    if (write_call_count % 100 == 0) {
        printf("[I2S Write] Calls: %lu, Total written: %lu, Current buffer: %lu->%lu\n",
               write_call_count, total_written, buffered_before, buffered_samples);
    }

    return samples_written;
}

// ============================================================================
// バッファの空き容量を取得
// ============================================================================

uint32_t audio_out_i2s_get_free_space(void) {
    return (I2S_BUFFER_SIZE / 2) - buffered_samples;
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

    // ピンポンバッファ: 両方のバッファを事前に埋める（これが重要！）
    fill_dma_buffer(dma_buffer[0], I2S_DMA_BUFFER_SIZE);
    fill_dma_buffer(dma_buffer[1], I2S_DMA_BUFFER_SIZE);
    current_dma_buffer = 0;
    printf("  Both DMA buffers pre-filled\n");

    // PIO State Machine を有効化
    pio_sm_set_enabled(pio, sm, true);
    printf("  PIO SM enabled\n");

    // DMA を開始（buffer[0]から）
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

    // 無音で埋める
    memset(ring_buffer, 0, sizeof(ring_buffer));
    memset(dma_buffer, 0, sizeof(dma_buffer));
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

static void fill_dma_buffer(int32_t *buffer, uint32_t num_samples) {
    for (uint32_t i = 0; i < num_samples; i++) {
        if (buffered_samples > 0) {
            // リングバッファからステレオデータを読み出す
            int16_t left = ring_buffer[read_pos * 2];
            int16_t right = ring_buffer[read_pos * 2 + 1];

            // 32ビットワードにパック（上位16ビット=左、下位16ビット=右）
            buffer[i] = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;

            read_pos = (read_pos + 1) % (I2S_BUFFER_SIZE / 2);
            buffered_samples--;
        } else {
            // データがない場合は無音を出力（アンダーラン）
            buffer[i] = 0;
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

        // ピンポンバッファの正しい実装:
        // 1. 次のバッファ（すでに埋まっている）でDMAを即座に再開
        // 2. 今終わったバッファを再充填（次回のために）
        uint8_t finished_buffer = current_dma_buffer;
        uint8_t next_buffer = 1 - current_dma_buffer;

        // DMAを次のバッファで即座に再起動（遅延を最小化）
        dma_channel_set_read_addr(dma_channel, dma_buffer[next_buffer], true);

        // 終わったバッファを再充填（次回の使用のため）
        fill_dma_buffer(dma_buffer[finished_buffer], I2S_DMA_BUFFER_SIZE);

        // 現在のバッファインデックスを更新
        current_dma_buffer = next_buffer;
    }
}
