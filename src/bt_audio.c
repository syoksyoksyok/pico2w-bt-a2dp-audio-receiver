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
#include "hardware/sync.h"

#include "btstack.h"
#include "btstack_sbc.h"

// ============================================================================
// 内部変数
// ============================================================================

static pcm_data_callback_t pcm_callback = NULL;
static bt_audio_control_callback_t control_callback = NULL;
static bool is_connected = false;
static bool media_streaming = false;
static bd_addr_t active_address;
static bool active_address_valid = false;
static bd_addr_t takeover_address;
static bool takeover_pending = false;
static uint32_t current_sample_rate = AUDIO_SAMPLE_RATE;

// A2DP SBC デコーダー
static btstack_sbc_decoder_state_t sbc_decoder_state;
static btstack_sbc_mode_t sbc_mode = SBC_MODE_STANDARD;

// SBC コーデック設定（A2DP Sink用）
// これはスマホ側に「このデバイスが対応しているSBC設定」を伝える
static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO | AVDTP_SBC_JOINT_STEREO,  // 44.1kHz, Stereo/Joint Stereo
    0xFF,  // すべてのブロック長、サブバンド、割り当て方式をサポート
    2, 53  // Min bitpool = 2, Max bitpool = 53（標準SBCの範囲）
};

// SBC コーデック実際の設定（ネゴシエーション後に格納される）
static uint8_t media_sbc_codec_configuration[4];

// A2DP コネクション
static uint8_t sdp_avdtp_sink_service_buffer[SDP_AVDTP_SINK_BUFFER_SIZE];
static uint16_t a2dp_cid = 0;
static uint8_t local_seid = 1;

#if ENABLE_DEBUG_LOG
#define SBC_DEBUG_PARSE_ERROR() do { sbc_debug_parse_errors++; } while (0)

typedef struct {
    bool valid;
    uint8_t bitpool;
    uint32_t sample_rate;
    uint8_t channel_mode;
    uint8_t blocks;
    uint8_t subbands;
    uint8_t allocation_method;
} sbc_debug_params_t;

static volatile sbc_debug_params_t sbc_debug_current;
static volatile bool sbc_debug_changed = false;
static volatile bool sbc_debug_first_frame_seen = false;
static volatile uint32_t sbc_debug_parse_errors = 0;
static volatile uint32_t sbc_debug_frames = 0;
static volatile uint32_t media_packet_count = 0;
static volatile uint32_t media_total_bytes = 0;
static absolute_time_t last_sbc_debug_log_time;

static bool parse_sbc_debug_frame(const uint8_t *frame, uint16_t size, sbc_debug_params_t *params, uint16_t *frame_length);
static void update_sbc_debug_from_payload(const uint8_t *payload, uint16_t payload_size, uint8_t expected_frames);
static void reset_sbc_debug_state(void);
static const char *sbc_debug_channel_mode_name(uint8_t channel_mode);
static const char *sbc_debug_allocation_name(uint8_t allocation_method);
#else
#define SBC_DEBUG_PARSE_ERROR() do { } while (0)
#endif

// ============================================================================
// イベントハンドラー（前方宣言）
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void a2dp_sink_media_packet_handler(uint8_t seid, uint8_t *packet, uint16_t size);
static void handle_pcm_data(int16_t *data, int num_samples, int num_channels, int sample_rate, void *context);
static bool get_sbc_payload(uint8_t *packet, uint16_t size, uint8_t **payload, uint16_t *payload_size, uint8_t *num_frames);
static bool is_different_active_device(const bd_addr_t address);
static void request_takeover(const bd_addr_t new_address, const char *reason);
static void disconnect_active_for_takeover(const bd_addr_t new_address, const char *reason);
static void try_start_pending_takeover(void);

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

#if ENABLE_DEBUG_LOG
    last_sbc_debug_log_time = get_absolute_time();
#endif

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

void bt_audio_set_control_callback(bt_audio_control_callback_t callback) {
    control_callback = callback;
}

// ============================================================================
// 接続奪い取りサポート
// ============================================================================

static bool is_different_active_device(const bd_addr_t address) {
    return active_address_valid && memcmp(active_address, address, sizeof(bd_addr_t)) != 0;
}

static void request_takeover(const bd_addr_t new_address, const char *reason) {
    if (!is_different_active_device(new_address)) {
        return;
    }

    memcpy(takeover_address, new_address, sizeof(bd_addr_t));
    takeover_pending = true;
    disconnect_active_for_takeover(new_address, reason);
}

static void disconnect_active_for_takeover(const bd_addr_t new_address, const char *reason) {
    if (a2dp_cid == 0) {
        return;
    }

    uint16_t old_cid = a2dp_cid;
    if (control_callback) {
        control_callback(BT_AUDIO_CONTROL_TAKEOVER_STARTED, old_cid);
    }
    media_streaming = false;
    is_connected = false;

    printf("A2DP takeover: %s by %s (old CID: 0x%04x)\n",
           reason, bd_addr_to_str(new_address), old_cid);
    a2dp_sink_disconnect(old_cid);
}

static void try_start_pending_takeover(void) {
    if (!takeover_pending || a2dp_cid != 0) {
        return;
    }

    uint16_t cid = 0;
    bd_addr_t address;
    memcpy(address, takeover_address, sizeof(bd_addr_t));
    takeover_pending = false;

    uint8_t status = a2dp_sink_establish_stream(address, &cid);
    if (status != ERROR_CODE_SUCCESS) {
        printf("A2DP takeover: failed to start stream to %s, status 0x%02x\n",
               bd_addr_to_str(address), status);
        return;
    }

    a2dp_cid = cid;
    memcpy(active_address, address, sizeof(bd_addr_t));
    active_address_valid = true;
    media_streaming = false;
    is_connected = false;
    printf("A2DP takeover: starting stream to %s (CID: 0x%04x)\n",
           bd_addr_to_str(address), cid);
}

// ============================================================================
// PCM データハンドラー（SBC デコーダーから呼ばれる）
// ============================================================================

static void handle_pcm_data(int16_t *data, int num_samples, int num_channels, int sample_rate, void *context) {
    UNUSED(context);

    // サンプルレートの更新
    if (current_sample_rate != (uint32_t)sample_rate) {
        current_sample_rate = (uint32_t)sample_rate;
    }

    // ソフトウェアボリューム調整（クリッピング防止）
    #if SOFTWARE_VOLUME_PERCENT < 100
    {
        // num_samples はステレオペア数なので、実際のサンプル数は num_samples * num_channels
        int total_samples = num_samples * num_channels;
        for (int i = 0; i < total_samples; i++) {
            int32_t scaled = (int32_t)data[i] * (int32_t)SOFTWARE_VOLUME_PERCENT;
            if (scaled >= 0) {
                scaled = (scaled + 50) / 100;
            } else {
                scaled = (scaled - 50) / 100;
            }
            if (scaled > INT16_MAX) {
                scaled = INT16_MAX;
            } else if (scaled < INT16_MIN) {
                scaled = INT16_MIN;
            }
            data[i] = (int16_t)scaled;
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

                    takeover_pending = false;
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
                        if (control_callback) {
                            control_callback(BT_AUDIO_CONTROL_CONNECTION_RELEASED, cid);
                        }
                        a2dp_cid = 0;
                        is_connected = false;
                        media_streaming = false;
                        active_address_valid = false;
                    }
                    try_start_pending_takeover();
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
                    if (control_callback) {
                        control_callback(BT_AUDIO_CONTROL_STREAM_STARTING, cid);
                    }
#if ENABLE_DEBUG_LOG
                    reset_sbc_debug_state();
#endif
                    memcpy(active_address, address, sizeof(bd_addr_t));
                    active_address_valid = true;
                    is_connected = true;
                    media_streaming = true;
                    break;

                case A2DP_SUBEVENT_STREAM_STARTED:
                    cid = a2dp_subevent_stream_started_get_a2dp_cid(packet);
                    if (cid == a2dp_cid) {
                        if (control_callback) {
                            control_callback(BT_AUDIO_CONTROL_STREAM_STARTING, cid);
                        }
#if ENABLE_DEBUG_LOG
                        reset_sbc_debug_state();
#endif
                        media_streaming = true;
                        printf("Stream started - Audio playback begins (CID: 0x%04x)\n", cid);
                    }
                    break;

                case A2DP_SUBEVENT_STREAM_SUSPENDED:
                    cid = a2dp_subevent_stream_suspended_get_a2dp_cid(packet);
                    if (cid == a2dp_cid) {
                        if (control_callback) {
                            control_callback(BT_AUDIO_CONTROL_STREAM_STOPPED, cid);
                        }
                        media_streaming = false;
                        printf("Stream suspended - Audio playback paused (CID: 0x%04x)\n", cid);
                    }
                    break;

                case A2DP_SUBEVENT_STREAM_RELEASED:
                    cid = a2dp_subevent_stream_released_get_a2dp_cid(packet);
                    printf("Stream released (CID: 0x%04x)\n", cid);
                    if (cid == a2dp_cid) {
                        if (control_callback) {
                            control_callback(BT_AUDIO_CONTROL_STREAM_RELEASED, cid);
                        }
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
                request_takeover(address, "new ACL connection");
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
// A2DP RTP/SBC ヘッダー解析
// ============================================================================

static bool get_sbc_payload(uint8_t *packet, uint16_t size, uint8_t **payload, uint16_t *payload_size, uint8_t *num_frames) {
    if (size < 13) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    uint8_t rtp_version = packet[0] >> 6;
    if (rtp_version != 2) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    bool has_padding = (packet[0] & 0x20) != 0;
    bool has_extension = (packet[0] & 0x10) != 0;
    uint8_t csrc_count = packet[0] & 0x0f;

    uint32_t pos = 12u + ((uint32_t)csrc_count * 4u);
    if (pos > size) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    if (has_extension) {
        if ((uint32_t)size - pos < 4u) {
            SBC_DEBUG_PARSE_ERROR();
            return false;
        }
        uint16_t extension_words = big_endian_read_16(packet, pos + 2u);
        pos += 4u + ((uint32_t)extension_words * 4u);
        if (pos > size) {
            SBC_DEBUG_PARSE_ERROR();
            return false;
        }
    }

    uint32_t end = size;
    if (has_padding) {
        uint8_t padding_len = packet[size - 1u];
        if (padding_len == 0 || padding_len > end - pos) {
            SBC_DEBUG_PARSE_ERROR();
            return false;
        }
        end -= padding_len;
    }

    if (end - pos < 1u) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    uint8_t sbc_header = packet[pos++];
    *num_frames = sbc_header & 0x0f;

    if (*num_frames == 0) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    if (pos >= end) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    *payload = packet + pos;
    *payload_size = (uint16_t)(end - pos);
    return true;
}

#if ENABLE_DEBUG_LOG
static bool parse_sbc_debug_frame(const uint8_t *frame, uint16_t size, sbc_debug_params_t *params, uint16_t *frame_length) {
    if (size < 4u || !params || !frame_length) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }
    if (frame[0] != 0x9cu) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    static const uint32_t sample_rates[] = {16000u, 32000u, 44100u, 48000u};
    static const uint8_t blocks_by_index[] = {4u, 8u, 12u, 16u};

    uint8_t frequency_index = (frame[1] >> 6) & 0x03u;
    uint8_t blocks_index = (frame[1] >> 4) & 0x03u;
    uint8_t channel_mode = (frame[1] >> 2) & 0x03u;
    uint8_t allocation_method = (frame[1] >> 1) & 0x01u;
    uint8_t subbands = (frame[1] & 0x01u) ? 8u : 4u;
    uint8_t bitpool = frame[2];
    uint8_t channels = (channel_mode == 0u) ? 1u : 2u;
    uint8_t blocks = blocks_by_index[blocks_index];

    if (bitpool == 0u) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    uint32_t length = 4u + ((4u * subbands * channels) + 7u) / 8u;
    if (channel_mode == 0u || channel_mode == 1u) {
        length += ((uint32_t)blocks * channels * bitpool + 7u) / 8u;
    } else if (channel_mode == 2u) {
        length += ((uint32_t)blocks * bitpool + 7u) / 8u;
    } else {
        length += (subbands + ((uint32_t)blocks * bitpool) + 7u) / 8u;
    }

    if (length > size || length > UINT16_MAX) {
        SBC_DEBUG_PARSE_ERROR();
        return false;
    }

    params->valid = true;
    params->bitpool = bitpool;
    params->sample_rate = sample_rates[frequency_index];
    params->channel_mode = channel_mode;
    params->blocks = blocks;
    params->subbands = subbands;
    params->allocation_method = allocation_method;
    *frame_length = (uint16_t)length;
    return true;
}

static void update_sbc_debug_from_payload(const uint8_t *payload, uint16_t payload_size, uint8_t expected_frames) {
    uint16_t pos = 0;
    uint8_t parsed_frames = 0;

    while (parsed_frames < expected_frames && pos < payload_size) {
        sbc_debug_params_t params = {0};
        uint16_t frame_length = 0;
        if (!parse_sbc_debug_frame(payload + pos, (uint16_t)(payload_size - pos), &params, &frame_length)) {
            return;
        }

        uint32_t save = save_and_disable_interrupts();
        bool changed = !sbc_debug_current.valid ||
                       sbc_debug_current.bitpool != params.bitpool ||
                       sbc_debug_current.sample_rate != params.sample_rate ||
                       sbc_debug_current.channel_mode != params.channel_mode ||
                       sbc_debug_current.blocks != params.blocks ||
                       sbc_debug_current.subbands != params.subbands ||
                       sbc_debug_current.allocation_method != params.allocation_method;

        sbc_debug_current = params;
        sbc_debug_frames++;
        if (!sbc_debug_first_frame_seen || changed) {
            sbc_debug_first_frame_seen = true;
            sbc_debug_changed = true;
        }
        restore_interrupts(save);

        pos = (uint16_t)(pos + frame_length);
        parsed_frames++;
    }

    if (parsed_frames != expected_frames) {
        SBC_DEBUG_PARSE_ERROR();
    }
}

static void reset_sbc_debug_state(void) {
    uint32_t save = save_and_disable_interrupts();
    memset((void *)&sbc_debug_current, 0, sizeof(sbc_debug_current));
    sbc_debug_changed = false;
    sbc_debug_first_frame_seen = false;
    sbc_debug_parse_errors = 0;
    sbc_debug_frames = 0;
    media_packet_count = 0;
    media_total_bytes = 0;
    last_sbc_debug_log_time = get_absolute_time();
    restore_interrupts(save);
}

static const char *sbc_debug_channel_mode_name(uint8_t channel_mode) {
    switch (channel_mode) {
        case 0: return "Mono";
        case 1: return "Dual Channel";
        case 2: return "Stereo";
        case 3: return "Joint Stereo";
        default: return "Unknown";
    }
}

static const char *sbc_debug_allocation_name(uint8_t allocation_method) {
    return allocation_method ? "Loudness" : "SNR";
}

void bt_audio_debug_log_process(void) {
    absolute_time_t now = get_absolute_time();
    bool print_change = false;
    bool print_stats = false;
    sbc_debug_params_t params = {0};
    uint32_t frames = 0;
    uint32_t errors = 0;
    uint32_t packets = 0;
    uint32_t bytes = 0;

    uint32_t save = save_and_disable_interrupts();
    if (sbc_debug_changed) {
        sbc_debug_changed = false;
        print_change = true;
    }
    int64_t elapsed_ms = absolute_time_diff_us(last_sbc_debug_log_time, now) / 1000;
    if (elapsed_ms >= BUFFER_STATUS_LOG_INTERVAL_MS) {
        last_sbc_debug_log_time = now;
        print_stats = true;
    }
    params = sbc_debug_current;
    frames = sbc_debug_frames;
    errors = sbc_debug_parse_errors;
    packets = media_packet_count;
    bytes = media_total_bytes;
    restore_interrupts(save);

    if ((print_change || print_stats) && params.valid) {
        printf("[SBC] bitpool=%u, rate=%lu Hz, mode=%s, blocks=%u, subbands=%u, alloc=%s, frames=%lu, parse_errors=%lu\n",
               params.bitpool,
               params.sample_rate,
               sbc_debug_channel_mode_name(params.channel_mode),
               params.blocks,
               params.subbands,
               sbc_debug_allocation_name(params.allocation_method),
               frames,
               errors);
    }

    if (print_stats && packets > 0u) {
        printf("[MEDIA] packets=%lu, bytes=%lu, avg_payload=%lu, sbc_parse_errors=%lu\n",
               packets, bytes, bytes / packets, errors);
    }
}
#endif

// ============================================================================
// A2DP Sink メディアパケットハンドラー（音声データを受信・デコード）
// ============================================================================

static void a2dp_sink_media_packet_handler(uint8_t seid, uint8_t *packet, uint16_t size) {
    UNUSED(seid);

    if (!media_streaming) {
        return;
    }

    uint8_t *sbc_payload = NULL;
    uint16_t sbc_payload_size = 0;
    uint8_t sbc_num_frames = 0;

    if (!get_sbc_payload(packet, size, &sbc_payload, &sbc_payload_size, &sbc_num_frames)) {
        return;
    }

#if ENABLE_DEBUG_LOG
    uint32_t debug_save = save_and_disable_interrupts();
    media_packet_count++;
    media_total_bytes += sbc_payload_size;
    restore_interrupts(debug_save);
    update_sbc_debug_from_payload(sbc_payload, sbc_payload_size, sbc_num_frames);
#endif

    // SBCデコーダーにSBCフレームデータだけを渡す
    btstack_sbc_decoder_process_data(&sbc_decoder_state, 0, sbc_payload, sbc_payload_size);
}
