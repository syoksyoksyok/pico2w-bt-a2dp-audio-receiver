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

void audio_out_i2s_init(uint32_t sample_rate, uint8_t bits_per_sample) {
    (void)sample_rate;
    (void)bits_per_sample;
    printf("I2S output is not available in SDK 2.2.0 - using PWM instead\n");
}

void audio_out_i2s_write(const int16_t *samples, size_t sample_count) {
    (void)samples;
    (void)sample_count;
    // No-op: I2S not available
}

void audio_out_i2s_set_volume(uint8_t volume_percent) {
    (void)volume_percent;
    // No-op: I2S not available
}
