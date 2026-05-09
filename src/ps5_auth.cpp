//
// PS5 authentication passthrough via Bluetooth HID Control channel
//
// When Pico is connected to PS5 via USB, PS5 sends authentication challenges
// as Feature Reports. We forward these to the real DS5 connected via BT,
// and cache the DS5's signed responses to return to PS5.
//

#include "ps5_auth.h"
#include "bt.h"
#include "utils.h"
#include <cstdio>
#include <cstring>

// Cached auth responses from DS5
static uint8_t auth_response_03[64];
static uint16_t auth_response_03_len = 0;
static uint8_t auth_response_04[64];
static uint16_t auth_response_04_len = 0;
static uint8_t auth_response_08[64];
static uint16_t auth_response_08_len = 0;
static bool response_03_ready = false;
static bool response_04_ready = false;
static bool response_08_ready = false;

bool ps5_auth_is_auth_report(uint8_t report_id) {
    return report_id == PS5_AUTH_SET_CHALLENGE ||
           report_id == PS5_AUTH_GET_RESPONSE ||
           report_id == PS5_AUTH_GET_STATUS;
}

void ps5_auth_forward_challenge(uint8_t report_id, const uint8_t *data, uint16_t len) {
    printf("[PS5Auth] Forwarding challenge 0x%02X len=%u to DS5 via BT\n", report_id, len);

    // Clear any previous response for the reports we'll be requesting
    if (report_id == PS5_AUTH_SET_CHALLENGE) {
        response_04_ready = false;
        response_08_ready = false;
    }

    // Forward as SET_REPORT via BT HID Control channel
    // HID SET_REPORT transaction: [0x53, report_id, data...]
    set_feature_data(report_id, const_cast<uint8_t *>(data), len);
}

bool ps5_auth_response_ready(uint8_t report_id) {
    switch (report_id) {
        case PS5_AUTH_SET_CHALLENGE: return response_03_ready;
        case PS5_AUTH_GET_RESPONSE:  return response_04_ready;
        case PS5_AUTH_GET_STATUS:    return response_08_ready;
        default: return false;
    }
}

uint16_t ps5_auth_get_response(uint8_t report_id, uint8_t *buffer, uint16_t max_len) {
    uint8_t *src = nullptr;
    uint16_t src_len = 0;

    switch (report_id) {
        case PS5_AUTH_GET_RESPONSE:
            src = auth_response_04;
            src_len = auth_response_04_len;
            break;
        case PS5_AUTH_GET_STATUS:
            src = auth_response_08;
            src_len = auth_response_08_len;
            break;
        default:
            return 0;
    }

    if (src_len == 0) {
        // No cached response, try to request from DS5
        printf("[PS5Auth] No cached response for 0x%02X, requesting from DS5\n", report_id);
        // This triggers a GET_REPORT via BT, response comes back asynchronously
        get_feature_data(report_id, max_len);
        return 0;
    }

    uint16_t copy_len = src_len < max_len ? src_len : max_len;
    memcpy(buffer, src, copy_len);
    printf("[PS5Auth] Returning cached response 0x%02X len=%u\n", report_id, copy_len);
    return copy_len;
}

void ps5_auth_on_bt_response(uint8_t report_id, const uint8_t *data, uint16_t len) {
    printf("[PS5Auth] Received BT response 0x%02X len=%u\n", report_id, len);

    switch (report_id) {
        case PS5_AUTH_SET_CHALLENGE:
            if (len <= sizeof(auth_response_03)) {
                memcpy(auth_response_03, data, len);
                auth_response_03_len = len;
                response_03_ready = true;
            }
            break;
        case PS5_AUTH_GET_RESPONSE:
            if (len <= sizeof(auth_response_04)) {
                memcpy(auth_response_04, data, len);
                auth_response_04_len = len;
                response_04_ready = true;
            }
            break;
        case PS5_AUTH_GET_STATUS:
            if (len <= sizeof(auth_response_08)) {
                memcpy(auth_response_08, data, len);
                auth_response_08_len = len;
                response_08_ready = true;
            }
            break;
    }
}

void ps5_auth_reset() {
    response_03_ready = false;
    response_04_ready = false;
    response_08_ready = false;
    auth_response_03_len = 0;
    auth_response_04_len = 0;
    auth_response_08_len = 0;
}
