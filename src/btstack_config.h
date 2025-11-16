/**
 * @file btstack_config.h
 * @brief BTstack configuration for Pico 2W A2DP Audio Receiver
 */

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ============================================================================
// BTstack Features
// ============================================================================

#define ENABLE_CLASSIC
// BLE is enabled at library level (required by pico_btstack_cyw43)
// but we disable peripheral/central roles since we only use Classic Bluetooth
#define ENABLE_LE_PERIPHERAL 0
#define ENABLE_LE_CENTRAL 0

// ============================================================================
// Classic Bluetooth Profiles
// ============================================================================

#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE

// ============================================================================
// BTstack Configuration
// ============================================================================

#define HCI_ACL_PAYLOAD_SIZE 1021
#define MAX_NR_AVDTP_CONNECTIONS 1
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 1
#define MAX_NR_AVRCP_CONNECTIONS 1
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 2
#define MAX_NR_GATT_CLIENTS 0
#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_L2CAP_CHANNELS 4
#define MAX_NR_L2CAP_SERVICES 3
#define MAX_NR_RFCOMM_CHANNELS 0
#define MAX_NR_RFCOMM_MULTIPLEXERS 0
#define MAX_NR_RFCOMM_SERVICES 0
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 1
#define MAX_NR_LE_DEVICE_DB_ENTRIES 0

// Link Key database for pairing
#define NVM_NUM_LINK_KEYS 2

// BLE settings (minimal, required by pico_btstack_ble library)
#define MAX_ATT_DB_SIZE 512
#define NVM_NUM_DEVICE_DB_ENTRIES 1

// CYW43 (Pico W/2W wireless chip) specific settings
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

// ============================================================================
// Logging and Debug
// ============================================================================

#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

#endif // BTSTACK_CONFIG_H
