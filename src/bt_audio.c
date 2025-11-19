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

// ============================================================================
// イベントハンドラー（前方宣言）
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void handle_pcm_data(int16_t *data, int num_samples, int num_channels, int sample_rate, void *context);

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

    // SDP レコードを登録（A2DP Sink として認識されるように）
    memset(sdp_avdtp_sink_service_buffer, 0, sizeof(sdp_avdtp_sink_service_buffer));
    a2dp_sink_create_sdp_record(sdp_avdtp_sink_service_buffer,
                                 0x10001,
                                 AVDTP_SINK_FEATURE_MASK_SPEAKER | AVDTP_SINK_FEATURE_MASK_AMPLIFIER,
                                 NULL, NULL);
    sdp_register_service(sdp_avdtp_sink_service_buffer);

    // SBC エンドポイントを登録
    avdtp_stream_endpoint_t *local_stream_endpoint = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO,
        AVDTP_CODEC_SBC,
        (uint8_t *)&sbc_decoder_state,
        sizeof(sbc_decoder_state),
        sdp_avdtp_sink_service_buffer,
        sizeof(sdp_avdtp_sink_service_buffer));

    if (!local_stream_endpoint) {
        printf("ERROR: Failed to create A2DP stream endpoint\n");
        return false;
    }

    local_seid = avdtp_local_seid(local_stream_endpoint);

    // SBC デコーダーの初期化
    btstack_sbc_decoder_init(&sbc_decoder_state, sbc_mode, &handle_pcm_data, NULL);

    // GAP（Generic Access Profile）の設定
    gap_discoverable_control(1);
    gap_set_class_of_device(BT_DEVICE_CLASS);
    gap_set_local_name(BT_DEVICE_NAME);

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
    // CYW43 (Wi-Fi/Bluetooth chip) のポーリング処理
    cyw43_arch_poll();

    // BTstack のメインループを実行（重要！これがないと音声パケットが処理されない）
    btstack_run_loop_poll();
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

    // サンプリングレートを更新
    if (current_sample_rate != (uint32_t)sample_rate) {
        current_sample_rate = (uint32_t)sample_rate;
        printf("Sample rate changed: %lu Hz\n", current_sample_rate);
    }

    // コールバックが設定されていればPCMデータを渡す
    if (pcm_callback) {
        pcm_callback(data, (uint32_t)num_samples, (uint8_t)num_channels, (uint32_t)sample_rate);
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

                    printf("SBC configuration %s: channels %d, sample rate %lu Hz\n",
                           reconfigure ? "reconfigured" : "received",
                           num_channels, sampling_frequency);

                    current_sample_rate = sampling_frequency;
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
