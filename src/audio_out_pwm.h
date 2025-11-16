/**
 * @file audio_out_pwm.h
 * @brief PWM 簡易 DAC オーディオ出力モジュール - ヘッダーファイル
 */

#ifndef AUDIO_OUT_PWM_H
#define AUDIO_OUT_PWM_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief PWM オーディオ出力を初期化
 * @param sample_rate サンプリングレート（Hz）
 * @return true: 成功, false: 失敗
 */
bool audio_out_pwm_init(uint32_t sample_rate);

/**
 * @brief PCM データをバッファに書き込む
 * @param pcm_data PCM データ（16bit signed, ステレオの場合は L+R を平均化してモノラルに変換）
 * @param num_samples サンプル数
 * @param channels チャンネル数（1: モノラル, 2: ステレオ）
 * @return 書き込んだサンプル数
 */
uint32_t audio_out_pwm_write(const int16_t *pcm_data, uint32_t num_samples, uint8_t channels);

/**
 * @brief バッファの空き容量を取得
 * @return 空きサンプル数
 */
uint32_t audio_out_pwm_get_free_space(void);

/**
 * @brief バッファ内のデータ量を取得
 * @return データサンプル数
 */
uint32_t audio_out_pwm_get_buffered_samples(void);

/**
 * @brief オーディオ出力を開始
 */
void audio_out_pwm_start(void);

/**
 * @brief オーディオ出力を停止
 */
void audio_out_pwm_stop(void);

/**
 * @brief バッファをクリア
 */
void audio_out_pwm_clear_buffer(void);

/**
 * @brief バッファ統計情報を取得（デバッグ用）
 * @param underruns アンダーラン回数
 * @param overruns オーバーラン回数
 */
void audio_out_pwm_get_stats(uint32_t *underruns, uint32_t *overruns);

#endif // AUDIO_OUT_PWM_H
