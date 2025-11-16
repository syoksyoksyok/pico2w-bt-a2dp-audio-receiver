/**
 * @file bt_audio.h
 * @brief Bluetooth A2DP Sink 管理モジュール - ヘッダーファイル
 */

#ifndef BT_AUDIO_H
#define BT_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Bluetooth A2DP モジュールを初期化
 * @return true: 成功, false: 失敗
 */
bool bt_audio_init(void);

/**
 * @brief Bluetooth スタックのメインループを実行
 * @note この関数は定期的に呼び出す必要があります
 */
void bt_audio_run(void);

/**
 * @brief 接続状態を取得
 * @return true: 接続中, false: 未接続
 */
bool bt_audio_is_connected(void);

/**
 * @brief 現在のサンプリングレートを取得
 * @return サンプリングレート（Hz）
 */
uint32_t bt_audio_get_sample_rate(void);

/**
 * @brief PCM データコールバック関数の型定義
 * @param pcm_data PCM データ（16bit signed, インターリーブ形式）
 * @param num_samples サンプル数（ステレオの場合、L/Rペアの数）
 * @param channels チャンネル数（1: モノラル, 2: ステレオ）
 * @param sample_rate サンプリングレート（Hz）
 */
typedef void (*pcm_data_callback_t)(const int16_t *pcm_data, uint32_t num_samples,
                                     uint8_t channels, uint32_t sample_rate);

/**
 * @brief PCM データコールバックを設定
 * @param callback コールバック関数
 */
void bt_audio_set_pcm_callback(pcm_data_callback_t callback);

#endif // BT_AUDIO_H
