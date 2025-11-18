/**
 * @file bt_audio.c
 * @brief Bluetooth A2DP Sink 管理モジュール
 */

#include "bt_audio.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "btstack.h"
#include "btstack_sbc.h"

// ============================================================================
// 内部変数
// ============================================================================

static pcm_data_callback_t pcm_callback = NULL;
static bool is_connected = false;
static uint32_t current_sample_rate = AUDIO_SAMPLE_RATE;

// A2DP SBC デコーダー
static btstack_sbc_decoder_state_t sbc_decoder_state;
static btstack_sbc_mode_t sbc_mode = SBC_MODE_STANDARD;

// PCM 出力バッファ
#define SBC_MAX_CHANNELS 2
// SBC_MAX_SAMPLES_PER_FRAME is already defined in btstack headers
static int16_t pcm_buffer[SBC_MAX_CHANNELS * SBC_MAX_SAMPLES_PER_FRAME];

// A2DP コネクション
static uint8_t sdp_avdtp_sink_service_buffer[150];
static uint16_t a2dp_cid = 0;
static uint8_t local_seid = 1;

// SBC Codec Capabilities (柔軟な設定をサポート)
static uint8_t sbc_codec_capabilities[] = {
    // Byte 0: サンプリング周波数 (上位4ビット) | チャンネルモード (下位4ビット)
    // すべてのサンプリングレートとチャンネルモードをサポート
    (AVDTP_SBC_44100 | AVDTP_SBC_48000) << 4 | (AVDTP_SBC_STEREO | AVDTP_SBC_JOINT_STEREO),

    // Byte 1: ブロック長 (上位4ビット) | サブバンド数 (bit 2-3) | アロケーション方法 (bit 0-1)
    (AVDTP_SBC_BLOCK_LENGTH_16 | AVDTP_SBC_BLOCK_LENGTH_12 | AVDTP_SBC_BLOCK_LENGTH_8 | AVDTP_SBC_BLOCK_LENGTH_4) << 4 |
    (AVDTP_SBC_SUBBANDS_8 | AVDTP_SBC_SUBBANDS_4) << 2 |
    (AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS | AVDTP_SBC_ALLOCATION_METHOD_SNR),

    2,    // Byte 2: 最小ビットプール値
    53    // Byte 3: 最大ビットプール値
};

// ============================================================================
// イベントハンドラー（前方宣言）
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void handle_pcm_data(int16_t *data, int num_samples, int num_channels, int sample_rate, void *context);
static void a2dp_sink_media_packet_handler(uint8_t seid, uint8_t *packet, uint16_t size);

// ============================================================================
// Bluetooth A2DP 初期化
// ============================================================================

bool bt_audio_init(void) {
    printf("\n========================================\n");
    printf("Pico 2W Bluetooth A2DP Audio Receiver\n");
    printf("========================================\n");
    printf("Initializing Bluetooth...\n");

    // CYW43 初期化（Pico W の Wi-Fi/Bluetooth チップ）
    if (cyw43_arch_init()) {
        printf("ERROR: Failed to initialize CYW43\n");
        return false;
    }
    printf("CYW43 initialized\n");

    // HCI の初期化
    l2cap_init();

    // SDP サーバーの初期化
    sdp_init();

    // A2DP Sink の初期化
    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&a2dp_sink_packet_handler);
    a2dp_sink_register_media_handler(&a2dp_sink_media_packet_handler);

    // SDP レコードを登録（A2DP Sink として認識されるように）
    memset(sdp_avdtp_sink_service_buffer, 0, sizeof(sdp_avdtp_sink_service_buffer));
    a2dp_sink_create_sdp_record(sdp_avdtp_sink_service_buffer,
                                 0x10001,
                                 AVDTP_SINK_FEATURE_MASK_SPEAKER | AVDTP_SINK_FEATURE_MASK_AMPLIFIER,
                                 NULL, NULL);
    sdp_register_service(sdp_avdtp_sink_service_buffer);

    // SBC エンドポイントを登録
    // 注: media_codec_information には SBC capabilities を渡す
    avdtp_stream_endpoint_t *local_stream_endpoint = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO,
        AVDTP_CODEC_SBC,
        sbc_codec_capabilities,
        sizeof(sbc_codec_capabilities),
        sdp_avdtp_sink_service_buffer,
        sizeof(sdp_avdtp_sink_service_buffer));

    if (!local_stream_endpoint) {
        printf("ERROR: Failed to create A2DP stream endpoint\n");
        return false;
    }

    local_seid = avdtp_local_seid(local_stream_endpoint);
    printf("A2DP stream endpoint created (SEID: %d)\n", local_seid);

    // SBC デコーダーの初期化
    btstack_sbc_decoder_init(&sbc_decoder_state, sbc_mode, &handle_pcm_data, NULL);

    // GAP（Generic Access Profile）の設定
    gap_discoverable_control(1);
    gap_set_class_of_device(BT_DEVICE_CLASS);
    gap_set_local_name(BT_DEVICE_NAME);

    // HCI イベントハンドラーの登録
    static btstack_packet_callback_registration_t hci_event_callback_registration;
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // HCI パワーオン
    hci_power_control(HCI_POWER_ON);

    printf("Bluetooth A2DP Sink initialized successfully\n");
    printf("Device name: %s\n", BT_DEVICE_NAME);
    printf("Waiting for connection...\n");
    printf("========================================\n\n");

    return true;
}

// ============================================================================
// Bluetooth スタックのメインループ
// ============================================================================

void bt_audio_run(void) {
    // BTstack のメインループを実行
    // この関数は定期的に呼び出す必要がある
}

// ============================================================================
// 接続状態の取得
// ============================================================================

bool bt_audio_is_connected(void) {
    return is_connected;
}

// ============================================================================
// サンプリングレートの取得
// ============================================================================

uint32_t bt_audio_get_sample_rate(void) {
    return current_sample_rate;
}

// ============================================================================
// PCM コールバックの設定
// ============================================================================

void bt_audio_set_pcm_callback(pcm_data_callback_t callback) {
    pcm_callback = callback;
}

// ============================================================================
// PCM データハンドラー（SBC デコーダーから呼ばれる）
// ============================================================================

static void handle_pcm_data(int16_t *data, int num_samples, int num_channels, int sample_rate, void *context) {
    UNUSED(context);

    // 初回のPCMデータ受信をログ出力
    static bool first_pcm = true;
    static uint32_t pcm_count = 0;
    static uint32_t last_logged_count = 0;
    pcm_count++;

    if (first_pcm) {
        printf("First PCM data received: %d samples, %d channels, %d Hz\n",
               num_samples, num_channels, sample_rate);
        first_pcm = false;
    }

    // 100回ごと、または1000回以上ログが出ていない場合に出力
    if (pcm_count % 100 == 0 || (pcm_count - last_logged_count) >= 1000) {
        printf("PCM callbacks: %lu (samples=%d, ch=%d)\n", pcm_count, num_samples, num_channels);
        last_logged_count = pcm_count;
    }

    // サンプリングレートを更新
    if (current_sample_rate != (uint32_t)sample_rate) {
        current_sample_rate = (uint32_t)sample_rate;
        printf("Sample rate changed: %lu Hz\n", current_sample_rate);
    }

    // データの健全性チェック（最初の数回のみ）
    if (pcm_count <= 3 && data != NULL) {
        // 最初のサンプル値を表示（デバッグ用）
        printf(">>> PCM sample[0]=%d, sample[1]=%d\n", data[0], data[1]);
    }

    // コールバックが設定されていればPCMデータを渡す
    if (pcm_callback) {
        pcm_callback(data, (uint32_t)num_samples, (uint8_t)num_channels, (uint32_t)sample_rate);
    } else {
        printf("WARNING: pcm_callback is NULL!\n");
    }
}

// ============================================================================
// A2DP Sink パケットハンドラー
// ============================================================================

static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    uint8_t status;
    bd_addr_t address;
    uint16_t cid;

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_A2DP_META:
            switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
                case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
                    a2dp_subevent_signaling_connection_established_get_bd_addr(packet, address);
                    cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
                    status = a2dp_subevent_signaling_connection_established_get_status(packet);

                    if (status != ERROR_CODE_SUCCESS) {
                        printf("A2DP connection failed, status 0x%02x\n", status);
                        break;
                    }

                    a2dp_cid = cid;
                    printf("A2DP connection established: %s (CID: 0x%04x)\n",
                           bd_addr_to_str(address), cid);
                    break;

                case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
                    printf("A2DP connection released\n");
                    a2dp_cid = 0;
                    is_connected = false;
                    break;

                case A2DP_SUBEVENT_STREAM_ESTABLISHED:
                    a2dp_subevent_stream_established_get_bd_addr(packet, address);
                    status = a2dp_subevent_stream_established_get_status(packet);

                    if (status != ERROR_CODE_SUCCESS) {
                        printf("Stream establishment failed, status 0x%02x\n", status);
                        is_connected = false;
                        break;
                    }

                    printf("Stream established: %s\n", bd_addr_to_str(address));
                    is_connected = true;
                    break;

                case A2DP_SUBEVENT_STREAM_STARTED:
                    printf("Stream started - Audio playback begins\n");
                    break;

                case A2DP_SUBEVENT_STREAM_SUSPENDED:
                    printf("Stream suspended - Audio playback paused\n");
                    break;

                case A2DP_SUBEVENT_STREAM_RELEASED:
                    printf("Stream released\n");
                    is_connected = false;
                    break;

                case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION: {
                    uint8_t reconfigure = a2dp_subevent_signaling_media_codec_sbc_configuration_get_reconfigure(packet);
                    uint8_t num_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(packet);
                    uint32_t sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);

                    printf("SBC configuration %s:\n",
                           reconfigure ? "reconfigured" : "received");
                    printf("  Channels: %d\n", num_channels);
                    printf("  Sample rate: %lu Hz\n", sampling_frequency);

                    current_sample_rate = sampling_frequency;

                    // SBCデコーダーを再初期化（ストリーム設定で）
                    printf("  Reinitializing SBC decoder with new configuration...\n");
                    btstack_sbc_decoder_init(&sbc_decoder_state, sbc_mode, &handle_pcm_data, NULL);
                    printf("  SBC decoder reinitialized\n");

                    break;
                }

                default:
                    break;
            }
            break;

        case HCI_EVENT_AVDTP_META:
            switch (packet[2]) {
                case AVDTP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
                    // Sink なので特に処理なし
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// 汎用パケットハンドラー（将来の拡張用）
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_PIN_CODE_REQUEST: {
            // PIN コードリクエスト（必要に応じて処理）
            printf("PIN code request - using default: 0000\n");
            bd_addr_t addr;
            hci_event_pin_code_request_get_bd_addr(packet, addr);
            gap_pin_code_response(addr, "0000");
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// A2DP Sink メディアパケットハンドラー（SBC データの受信とデコード）
// ============================================================================

static void a2dp_sink_media_packet_handler(uint8_t seid, uint8_t *packet, uint16_t size) {
    UNUSED(seid);

    // デバッグ出力（初回と定期的に）
    static bool first_packet = true;
    static uint32_t packet_count = 0;
    packet_count++;

    if (first_packet) {
        printf("\n=== FULL PACKET DUMP ===\n");
        printf("Total packet size: %d bytes\n", size);
        printf("First 60 bytes:\n");
        int dump_size = size > 60 ? 60 : size;
        for (int i = 0; i < dump_size; i++) {
            printf("%02X ", packet[i]);
            if ((i + 1) % 16 == 0) printf("  [offset %d]\n", i + 1 - 16);
        }
        if (dump_size % 16 != 0) printf("  [offset %d]\n", (dump_size / 16) * 16);

        // 0x9C (SBC sync word) を探す
        printf("\nSearching for SBC sync word (0x9C):\n");
        for (int i = 0; i < size - 1; i++) {
            if (packet[i] == 0x9C) {
                printf("  Found 0x9C at offset %d\n", i);
            }
        }
        printf("=========================\n\n");
        first_packet = false;
    }

    // パケットサイズチェック
    if (size < 13) {
        // RTP(12) + AVDTP(1) の最小サイズ未満
        return;
    }

    // RTP ヘッダーをスキップ（12バイト）
    int pos = 12;

    // AVDTPメディアペイロードヘッダー（1バイト）をスキップ
    pos++;

    // SBCフレーム数を取得（1バイト）
    if (pos >= size) return;
    uint8_t num_frames = packet[pos];
    pos++;

    // 100パケットごとにログ出力
    if (packet_count % 100 == 0) {
        printf("Media packets received: %lu (frames=%d, size=%d)\n", packet_count, num_frames, size);
    }

    // SBCデータ全体をデコーダーに渡す
    // BTstackのSBCデコーダーは内部で全フレームを処理し、PCMコールバックを呼び出す
    btstack_sbc_decoder_process_data(&sbc_decoder_state, 0, packet + pos, size - pos);

    if (packet_count <= 5) {
        printf(">>> SBC decoder returned\n");
    }
}
