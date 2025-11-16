/**
 * @file audio_out_i2s.h
 * @brief I2S DAC オーディオ出力モジュール - ヘッダーファイル
 */

#ifndef AUDIO_OUT_I2S_H
#define AUDIO_OUT_I2S_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief I2S オーディオ出力を初期化
 * @param sample_rate サンプリングレート（Hz）
 * @param bits_per_sample ビット深度（16, 24, 32）
 * @param channels チャンネル数（1: モノラル, 2: ステレオ）
 * @return true: 成功, false: 失敗
 */
bool audio_out_i2s_init(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels);

/**
 * @brief PCM データをバッファに書き込む
 * @param pcm_data PCM データ（16bit signed, インターリーブ形式）
 * @param num_samples サンプル数（ステレオの場合、L/Rペアの数）
 * @return 書き込んだサンプル数
 */
uint32_t audio_out_i2s_write(const int16_t *pcm_data, uint32_t num_samples);

/**
 * @brief バッファの空き容量を取得
 * @return 空きサンプル数
 */
uint32_t audio_out_i2s_get_free_space(void);

/**
 * @brief バッファ内のデータ量を取得
 * @return データサンプル数
 */
uint32_t audio_out_i2s_get_buffered_samples(void);

/**
 * @brief オーディオ出力を開始
 */
void audio_out_i2s_start(void);

/**
 * @brief オーディオ出力を停止
 */
void audio_out_i2s_stop(void);

/**
 * @brief バッファをクリア
 */
void audio_out_i2s_clear_buffer(void);

/**
 * @brief バッファ統計情報を取得（デバッグ用）
 * @param underruns アンダーラン回数
 * @param overruns オーバーラン回数
 */
void audio_out_i2s_get_stats(uint32_t *underruns, uint32_t *overruns);

#endif // AUDIO_OUT_I2S_H
