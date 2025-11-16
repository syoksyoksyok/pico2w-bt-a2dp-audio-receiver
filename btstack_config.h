/**
 * @file btstack_config.h
 * @brief BTstack configuration for Pico 2W A2DP Audio Receiver
 */

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ====================================================================================
// BTstack features
// ====================================================================================

// Avoid redefinition warnings (Pico SDK defines this via command line)
#ifndef ENABLE_CLASSIC
#define ENABLE_CLASSIC
#endif

// BLE must be enabled for Pico W BTstack even if we only use Classic Bluetooth
#define ENABLE_BLE

// ====================================================================================
// Bluetooth profiles
// ====================================================================================

// A2DP Sink support (receive audio)
#define ENABLE_A2DP_SINK
#define ENABLE_AVDTP_SINK

// AVRCP support (media control)
#define ENABLE_AVRCP
#define ENABLE_AVRCP_CONTROLLER

// ====================================================================================
// BTstack modules
// ====================================================================================

#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE

// ====================================================================================
// BTstack configuration
// ====================================================================================

// HCI ACL buffer size
#define HCI_ACL_PAYLOAD_SIZE (1021 + 4)

// HCI buffer configuration for CYW43
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

// Link Key DB size
#define NVM_NUM_LINK_KEYS 4

// ATT DB size (required for BLE)
#define MAX_ATT_DB_SIZE 512

// Logging
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

// ====================================================================================
// Port specific configuration
// ====================================================================================

#define HAVE_EMBEDDED_TIME_MS

// Pico SDK provides malloc
#define HAVE_MALLOC

// Disable BTstack assertions (no implementation needed)
// #define ENABLE_BTSTACK_ASSERT

#endif // BTSTACK_CONFIG_H
