/**
 * @file audio_out_i2s.c
 * @brief I2S DAC オーディオ出力モジュール（スタブ実装）
 *
 * Note: SDK 2.2.0にはhardware_i2sライブラリが存在しないため、
 * I2S機能は無効化されています。PWM出力を使用してください。
 */

#include "audio_out_i2s.h"
#include "config.h"
#include <stdio.h>

bool audio_out_i2s_init(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels) {
    (void)sample_rate;
    (void)bits_per_sample;
    (void)channels;
    printf("I2S output is not available in SDK 2.2.0 - using PWM instead\n");
    return false;  // I2S not available
}

uint32_t audio_out_i2s_write(const int16_t *pcm_data, uint32_t num_samples) {
    (void)pcm_data;
    (void)num_samples;
    return 0;  // No samples written
}

uint32_t audio_out_i2s_get_free_space(void) {
    return 0;  // No buffer available
}

uint32_t audio_out_i2s_get_buffered_samples(void) {
    return 0;  // No buffer available
}

void audio_out_i2s_start(void) {
    // No-op: I2S not available
}

void audio_out_i2s_stop(void) {
    // No-op: I2S not available
}

void audio_out_i2s_clear_buffer(void) {
    // No-op: I2S not available
}

void audio_out_i2s_get_stats(uint32_t *underruns, uint32_t *overruns) {
    if (underruns) *underruns = 0;
    if (overruns) *overruns = 0;
}
