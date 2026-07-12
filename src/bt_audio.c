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
static bool media_streaming = false;
static bd_addr_t active_address;
static bool active_address_valid = false;
static uint32_t current_sample_rate = AUDIO_SAMPLE_RATE;

// A2DP SBC デコーダー
static btstack_sbc_decoder_state_t sbc_decoder_state;
static btstack_sbc_mode_t sbc_mode = SBC_MODE_STANDARD;

// SBC コーデック設定（A2DP Sink用）
// これはスマホ側に「このデバイスが対応しているSBC設定」を伝える
static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,  // 44.1kHz, ステレオ
    0xFF,  // すべてのブロック長、サブバンド、割り当て方式をサポート
    2, 35  // Min bitpool = 2, Max bitpool = 35（安定性優先）
};

// SBC コーデック実際の設定（ネゴシエーション後に格納される）
static uint8_t media_sbc_codec_configuration[4];

// A2DP コネクション
static uint8_t sdp_avdtp_sink_service_buffer[SDP_AVDTP_SINK_BUFFER_SIZE];
static uint16_t a2dp_cid = 0;
static uint8_t local_seid = 1;

// ============================================================================
// イベントハンドラー（前方宣言）
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void a2dp_sink_media_packet_handler(uint8_t seid, uint8_t *packet, uint16_t size);
static void handle_pcm_data(int16_t *data, int num_samples, int num_channels, int sample_rate, void *context);
static bool is_different_active_device(const bd_addr_t address);
static void disconnect_active_for_takeover(const bd_addr_t new_address, const char *reason);

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
    printf("CYW43 initialized (poll mode)\n");

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
    // 重要: コーデックのcapabilitiesとconfigurationバッファを渡す
    avdtp_stream_endpoint_t *local_stream_endpoint = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO,
        AVDTP_CODEC_SBC,
        media_sbc_codec_capabilities,      // ← このデバイスが対応するSBC設定
        sizeof(media_sbc_codec_capabilities),
        media_sbc_codec_configuration,     // ← ネゴシエーション後の実際の設定
        sizeof(media_sbc_codec_configuration));

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
    gap_connectable_control(1);
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
    // CYW43のポーリング（WiFi/Bluetoothチップの処理）
    cyw43_arch_poll();

    // 非同期コンテキストのポーリング（BTstackイベント処理）
    // これがないとメディアパケットが処理されない！
    async_context_poll(cyw43_arch_async_context());
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
// 接続奪い取りサポート
// ============================================================================

static bool is_different_active_device(const bd_addr_t address) {
    return active_address_valid && memcmp(active_address, address, sizeof(bd_addr_t)) != 0;
}

static void disconnect_active_for_takeover(const bd_addr_t new_address, const char *reason) {
    if (a2dp_cid == 0) {
        return;
    }

    uint16_t old_cid = a2dp_cid;
    media_streaming = false;
    is_connected = false;

    printf("A2DP takeover: %s by %s (old CID: 0x%04x)\n",
           reason, bd_addr_to_str(new_address), old_cid);
    a2dp_sink_disconnect(old_cid);
}

// ============================================================================
// PCM データハンドラー（SBC デコーダーから呼ばれる）
// ============================================================================

static void handle_pcm_data(int16_t *data, int num_samples, int num_channels, int sample_rate, void *context) {
    UNUSED(context);

    static uint32_t pcm_callback_count = 0;
    pcm_callback_count++;

#if ENABLE_DEBUG_LOG
    // 最初の数回だけログ出力（デバッグ用）
    if (pcm_callback_count <= INITIAL_PCM_LOG_COUNT) {
        printf("[PCM] Received: %d samples, %d ch, %d Hz\n", num_samples, num_channels, sample_rate);
    }
#endif

    // サンプルレートの更新
    if (current_sample_rate != (uint32_t)sample_rate) {
        current_sample_rate = (uint32_t)sample_rate;
        printf("Sample rate: %lu Hz\n", current_sample_rate);
    }

    // ソフトウェアボリューム調整（クリッピング防止）
    #if SOFTWARE_VOLUME_PERCENT < 100
    {
        // num_samples はステレオペア数なので、実際のサンプル数は num_samples * num_channels
        int total_samples = num_samples * num_channels;
        for (int i = 0; i < total_samples; i++) {
            // 音量を調整（オーバーフロー防止のため32ビットで計算）
            int32_t sample = data[i];
            sample = (sample * SOFTWARE_VOLUME_PERCENT) / 100;
            data[i] = (int16_t)sample;
        }
    }
    #endif

    // PCMコールバックに渡す
    // 重要: BTstackのSBCデコーダーは num_samples を「ステレオペア数」として渡す
    // つまり num_samples=128 は 128ステレオペア = 256個のint16_t (左128+右128)
    // audio_out_i2s_write()も「ステレオペア数」を期待しているので、そのまま渡す
    if (pcm_callback) {
        // 2で割らない！そのまま渡す
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

                    if (a2dp_cid != 0 && a2dp_cid != cid) {
                        disconnect_active_for_takeover(address, "A2DP signaling connection");
                    }

                    a2dp_cid = cid;
                    memcpy(active_address, address, sizeof(bd_addr_t));
                    active_address_valid = true;
                    media_streaming = false;
                    printf("A2DP connection established: %s (CID: 0x%04x)\n",
                           bd_addr_to_str(address), cid);
                    break;

                case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
                    cid = a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet);
                    printf("A2DP connection released (CID: 0x%04x)\n", cid);
                    if (cid == a2dp_cid) {
                        a2dp_cid = 0;
                        is_connected = false;
                        media_streaming = false;
                        active_address_valid = false;
                    }
                    break;

                case A2DP_SUBEVENT_STREAM_ESTABLISHED:
                    a2dp_subevent_stream_established_get_bd_addr(packet, address);
                    cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
                    status = a2dp_subevent_stream_established_get_status(packet);

                    if (status != ERROR_CODE_SUCCESS) {
                        printf("Stream establishment failed, status 0x%02x (CID: 0x%04x)\n", status, cid);
                        if (cid == a2dp_cid) {
                            is_connected = false;
                            media_streaming = false;
                        }
                        break;
                    }

                    if (cid != a2dp_cid) {
                        printf("Ignoring stream from stale A2DP connection: %s (CID: 0x%04x)\n",
                               bd_addr_to_str(address), cid);
                        break;
                    }

                    printf("Stream established: %s (CID: 0x%04x)\n", bd_addr_to_str(address), cid);
                    memcpy(active_address, address, sizeof(bd_addr_t));
                    active_address_valid = true;
                    is_connected = true;
                    media_streaming = false;
                    break;

                case A2DP_SUBEVENT_STREAM_STARTED:
                    cid = a2dp_subevent_stream_started_get_a2dp_cid(packet);
                    if (cid == a2dp_cid) {
                        media_streaming = true;
                        printf("Stream started - Audio playback begins (CID: 0x%04x)\n", cid);
                    }
                    break;

                case A2DP_SUBEVENT_STREAM_SUSPENDED:
                    cid = a2dp_subevent_stream_suspended_get_a2dp_cid(packet);
                    if (cid == a2dp_cid) {
                        media_streaming = false;
                        printf("Stream suspended - Audio playback paused (CID: 0x%04x)\n", cid);
                    }
                    break;

                case A2DP_SUBEVENT_STREAM_RELEASED:
                    cid = a2dp_subevent_stream_released_get_a2dp_cid(packet);
                    printf("Stream released (CID: 0x%04x)\n", cid);
                    if (cid == a2dp_cid) {
                        is_connected = false;
                        media_streaming = false;
                    }
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
        case HCI_EVENT_CONNECTION_COMPLETE: {
            if (hci_event_connection_complete_get_status(packet) != ERROR_CODE_SUCCESS) {
                break;
            }
            if (hci_event_connection_complete_get_link_type(packet) != HCI_LINK_TYPE_ACL) {
                break;
            }

            bd_addr_t address;
            hci_event_connection_complete_get_bd_addr(packet, address);
            if (a2dp_cid != 0 && is_different_active_device(address)) {
                disconnect_active_for_takeover(address, "new ACL connection");
            }
            break;
        }

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
// A2DP Sink メディアパケットハンドラー（音声データを受信・デコード）
// ============================================================================

static void a2dp_sink_media_packet_handler(uint8_t seid, uint8_t *packet, uint16_t size) {
    UNUSED(seid);

    if (!media_streaming) {
        return;
    }

    static uint32_t media_packet_count = 0;
    static uint32_t media_total_bytes = 0;

    media_packet_count++;
    media_total_bytes += (size > SBC_MEDIA_PACKET_HEADER_OFFSET) ?
                         (size - SBC_MEDIA_PACKET_HEADER_OFFSET) : 0;

#if ENABLE_DEBUG_LOG
    uint16_t media_data_size = (size > SBC_MEDIA_PACKET_HEADER_OFFSET) ?
                               (size - SBC_MEDIA_PACKET_HEADER_OFFSET) : 0;

    // 最初の数回だけログ出力（デバッグ用）
    if (media_packet_count <= INITIAL_MEDIA_LOG_COUNT) {
        printf("[MEDIA] Packet #%lu: size=%u, offset=%d, data_size=%u\n",
               media_packet_count, size, SBC_MEDIA_PACKET_HEADER_OFFSET,
               media_data_size);
    }

    // N回ごとに統計を表示（頻度はconfig.hで設定）
    if (media_packet_count % STATS_LOG_FREQUENCY == 0) {
        printf("[MEDIA Stats] Packets: %lu, Total bytes: %lu, Avg size: %lu\n",
               media_packet_count, media_total_bytes, media_total_bytes / media_packet_count);
    }
#endif

    // メディアパケットサイズの検証
    if (size < SBC_MEDIA_PACKET_HEADER_OFFSET) {
        printf("[MEDIA] ERROR: Packet too small (%u bytes, expected >= %d)\n",
               size, SBC_MEDIA_PACKET_HEADER_OFFSET);
        return;
    }

    // SBCデコーダーにデータを渡す（ヘッダー13バイトをスキップ）
    btstack_sbc_decoder_process_data(&sbc_decoder_state, 0,
                                      packet + SBC_MEDIA_PACKET_HEADER_OFFSET,
                                      size - SBC_MEDIA_PACKET_HEADER_OFFSET);
}
