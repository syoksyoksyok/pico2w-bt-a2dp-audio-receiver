/**
 * @file config.h
 * @brief Pico 2W Bluetooth A2DP Audio Receiver - 設定ファイル
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// Bluetooth 設定
// ============================================================================

// デバイス名（Bluetooth接続時に表示される名前）
#define BT_DEVICE_NAME "Pico2W Audio Receiver"

// Bluetooth クラス（オーディオデバイスとして認識されるように設定）
#define BT_DEVICE_CLASS 0x200420  // Audio/Video - Loudspeaker

// ============================================================================
// オーディオ出力モード選択
// ============================================================================

// 出力モードを選択（どちらか一方をコメントアウト）
// NOTE: I2S implementation is not available in current SDK version
// Use PWM output instead
// #define USE_I2S_OUTPUT     1   // I2S DAC 出力を使用（未実装）
#define USE_PWM_OUTPUT     1   // PWM 簡易 DAC 出力を使用

// ============================================================================
// I2S DAC 設定（PCM5102A などの I2S DAC を使用する場合）
// ============================================================================

#ifdef USE_I2S_OUTPUT

// I2S ピン配置
#define I2S_DATA_PIN    26    // DIN (Data)
#define I2S_BCLK_PIN    27    // BCK (Bit Clock)
#define I2S_LRCLK_PIN   28    // LCK (Left/Right Clock / Word Select)

// サンプリングレート（Hz）
#define AUDIO_SAMPLE_RATE    44100

// ビット深度
#define AUDIO_BITS_PER_SAMPLE 16

// チャンネル数（ステレオ）
#define AUDIO_CHANNELS        2

#endif // USE_I2S_OUTPUT

// ============================================================================
// PWM 簡易 DAC 設定（PWM/PIO を使用する場合）
// ============================================================================

// PWM 出力ピン（モノラル出力）
#ifndef PWM_AUDIO_PIN
#define PWM_AUDIO_PIN    26
#endif

// PWM 解像度（ビット）
#ifndef PWM_RESOLUTION_BITS
#define PWM_RESOLUTION_BITS  8
#endif

#ifdef USE_PWM_OUTPUT
// サンプリングレート（Hz）
#define AUDIO_SAMPLE_RATE    44100

// チャンネル数（モノラル）
#define AUDIO_CHANNELS        1

#endif // USE_PWM_OUTPUT

// ============================================================================
// オーディオバッファ設定
// ============================================================================

// リングバッファサイズ（サンプル数）
// 大きいほど安定するが、遅延も増える
#define AUDIO_BUFFER_SIZE    (AUDIO_SAMPLE_RATE * 2)  // 2秒分

// DMA バッファサイズ（サンプル数）
#define DMA_BUFFER_SIZE      512

// バッファアンダーラン/オーバーラン検出の閾値
#define BUFFER_LOW_THRESHOLD    (AUDIO_BUFFER_SIZE / 4)
#define BUFFER_HIGH_THRESHOLD   (AUDIO_BUFFER_SIZE * 3 / 4)

// ============================================================================
// デバッグログ設定
// ============================================================================

// デバッグログを有効化
#define ENABLE_DEBUG_LOG     1

// バッファ状態のログ出力間隔（ミリ秒）
#define BUFFER_STATUS_LOG_INTERVAL_MS  5000

#endif // CONFIG_H
