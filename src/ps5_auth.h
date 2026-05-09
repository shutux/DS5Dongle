//
// PS5 authentication passthrough via Bluetooth HID Control channel
//

#ifndef DS5_BRIDGE_PS5_AUTH_H
#define DS5_BRIDGE_PS5_AUTH_H

#include <cstdint>

// PS5 authentication Feature Report IDs
#define PS5_AUTH_SET_CHALLENGE    0x03  // PS5 → Pico: challenge nonce
#define PS5_AUTH_GET_RESPONSE     0x04  // PS5 ← Pico: signed response
#define PS5_AUTH_GET_STATUS       0x08  // PS5 ← Pico: auth status

// Forward a PS5 authentication challenge to the connected DS5 via BT HID Control
// report_id: 0x03
// data: the challenge data (excluding report_id)
// len: data length
void ps5_auth_forward_challenge(uint8_t report_id, const uint8_t *data, uint16_t len);

// Check if a Feature Report ID is a PS5 auth request
bool ps5_auth_is_auth_report(uint8_t report_id);

// Check if we have a pending auth response ready
bool ps5_auth_response_ready(uint8_t report_id);

// Get the cached auth response for a given report ID
// Returns the response length, 0 if not available
uint16_t ps5_auth_get_response(uint8_t report_id, uint8_t *buffer, uint16_t max_len);

// Called when we receive a HID Control response from DS5 via BT
void ps5_auth_on_bt_response(uint8_t report_id, const uint8_t *data, uint16_t len);

// Reset auth state (on disconnect)
void ps5_auth_reset();

#endif // DS5_BRIDGE_PS5_AUTH_H
