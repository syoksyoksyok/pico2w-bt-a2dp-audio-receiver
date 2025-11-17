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
static void handle_l2cap_media_data_packet(uint8_t seid, uint8_t *packet, uint16_t size);

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
    a2dp_sink_register_media_handler(&handle_l2cap_media_data_packet);

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

    static uint32_t pcm_packet_count = 0;
    pcm_packet_count++;

    // サンプリングレートを更新
    if (current_sample_rate != (uint32_t)sample_rate) {
        current_sample_rate = (uint32_t)sample_rate;
        printf("Sample rate changed: %lu Hz\n", current_sample_rate);
    }

    // 最初の数パケットだけログ出力
    if (pcm_packet_count <= 5) {
        printf("[PCM] Decoded: samples=%d, channels=%d, rate=%d Hz (packet #%lu)\n",
               num_samples, num_channels, sample_rate, pcm_packet_count);
    }

    // コールバックが設定されていればPCMデータを渡す
    if (pcm_callback) {
        pcm_callback(data, (uint32_t)num_samples, (uint8_t)num_channels, (uint32_t)sample_rate);
    } else {
        printf("[PCM] WARNING: No callback registered!\n");
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

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("[BT] DISCONNECTION_COMPLETE event\n");
            break;

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
                    printf("[A2DP] Connection RELEASED (disconnected)\n");
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
                    printf("[A2DP] Stream STARTED - Audio playback begins\n");
                    break;

                case A2DP_SUBEVENT_STREAM_SUSPENDED:
                    printf("[A2DP] Stream SUSPENDED - Audio playback paused\n");
                    break;

                case A2DP_SUBEVENT_STREAM_RELEASED:
                    printf("[A2DP] Stream RELEASED\n");
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
// L2CAP メディアデータパケットハンドラー
// ============================================================================

static void handle_l2cap_media_data_packet(uint8_t seid, uint8_t *packet, uint16_t size) {
    UNUSED(seid);

    printf("[A2DP] Media packet received: size=%d\n", size);

    // パケットサイズチェック
    if (size < 13) {
        printf("[A2DP] Packet too small: %d bytes\n", size);
        return;
    }

    int pos = 0;

    // メディアパケットヘッダーの読み取り (12 bytes)
    // RTP ヘッダー構造:
    // - V(2), P(1), X(1), CC(4) [1 byte]
    // - M(1), PT(7) [1 byte]
    // - Sequence Number [2 bytes]
    // - Timestamp [4 bytes]
    // - SSRC [4 bytes]

    // RTP ヘッダーをスキップ (12 bytes)
    pos += 12;

    // SBC メディアペイロードヘッダーの読み取り (1 byte)
    // - フラグメンテーション情報とフレーム数
    if (pos >= size) {
        printf("[A2DP] No SBC header\n");
        return;
    }

    uint8_t sbc_media_header = packet[pos];
    pos += 1;

    // フラグメンテーションチェック
    // ビット7: フラグメンテーション (0 = 完全なフレーム, 1 = フラグメント)
    // ビット6-4: 開始/継続/終了フラグ
    // ビット3-0: フレーム数
    uint8_t fragmentation = (sbc_media_header >> 7) & 0x01;
    uint8_t num_frames = sbc_media_header & 0x0F;

    printf("[A2DP] SBC header: frag=%d, frames=%d\n", fragmentation, num_frames);

    if (fragmentation) {
        printf("[A2DP] Fragmented packet (unsupported)\n");
        return;
    }

    // SBC フレームデータを SBC デコーダーに渡す
    int remaining_size = size - pos;
    if (remaining_size > 0) {
        uint8_t *sbc_frame_data = packet + pos;

        printf("[A2DP] Decoding %d bytes of SBC data\n", remaining_size);

        // SBC デコーダーにデータを供給
        // デコーダーは自動的に handle_pcm_data コールバックを呼び出す
        btstack_sbc_decoder_process_data(&sbc_decoder_state, 0, sbc_frame_data, remaining_size);
    } else {
        printf("[A2DP] No SBC data to decode\n");
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
