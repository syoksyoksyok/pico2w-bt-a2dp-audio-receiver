/**
 * @file btstack_config.h
 * @brief BTstack configuration for Pico 2W A2DP Audio Receiver
 */

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ============================================================================
// BTstack Features - Classic Bluetooth Only (Disable BLE)
// ============================================================================

#define ENABLE_CLASSIC
#define ENABLE_BLE 0
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

// ============================================================================
// Disable Unused Features
// ============================================================================

#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR

#endif // BTSTACK_CONFIG_H
