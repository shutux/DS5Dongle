//
// Switch Pro Controller USB emulation (057E:2009)
// Full protocol: handshake, subcommands, SPI flash, IMU, HD Rumble
// Based on Demogorgon314/DS5Dongle ds5-to-switchpro branch
//

#include "switch_pro.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "bt.h"
#include "cmd.h"
#include "config.h"
#include "platform.h"
#include "pico/critical_section.h"
#include "pico/time.h"
#include "tusb.h"

namespace {

constexpr uint32_t REPORT_INTERVAL_US = 15000;   // 15ms between 0x30 reports
constexpr uint8_t  REPORT_SIZE = 63;              // payload size (without report ID)
constexpr uint8_t  DEVICE_TYPE = 0x03;            // Pro Controller
constexpr uint8_t  DS5_TRIGGER_THRESHOLD = 32;

constexpr int32_t DS5_ACCEL_RES_PER_G = 8192;
constexpr int32_t SWITCH_ACCEL_RES_PER_G = 4096;
constexpr int32_t DS5_EFFECTIVE_GYRO_RES_PER_DEG_S = 16;
constexpr int32_t SWITCH_GYRO_RES_PER_DEG_S_X1000 = 14284;

// ============================================================
// HID Report Descriptor — Pro Controller (219 bytes)
// Has Report IDs: 0x30/0x21/0x81 input, 0x01/0x10/0x80/0x82 output, 0xF6/0xF7 feature
// ============================================================
static const uint8_t hid_report_desc[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Controls)
    0x15, 0x00,       // Logical Minimum (0)
    0x09, 0x04,       // Usage (Joystick)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x30,       //   Report ID (0x30) — Full input report
    0x05, 0x01,       //   Usage Page (Generic Desktop Controls)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (1)
    0x29, 0x0A,       //   Usage Maximum (10)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x0A,       //   Report Count (10)
    0x55, 0x00,       //   Unit Exponent (0)
    0x65, 0x00,       //   Unit (None)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x0B,       //   Usage Minimum (11)
    0x29, 0x0E,       //   Usage Maximum (14)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x03,       //   Input (Const,Var,Abs)
    0x0B, 0x01, 0x00, 0x01, 0x00,
    0xA1, 0x00,       //   Collection (Physical)
    0x0B, 0x30, 0x00, 0x01, 0x00,
    0x0B, 0x31, 0x00, 0x01, 0x00,
    0x0B, 0x32, 0x00, 0x01, 0x00,
    0x0B, 0x35, 0x00, 0x01, 0x00,
    0x15, 0x00,       //     Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x04,       //     Report Count (4)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0xC0,             //   End Collection
    0x0B, 0x39, 0x00, 0x01, 0x00,
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x07,       //   Logical Maximum (7)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (Eng Rot:Angular Pos)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x0F,       //   Usage Minimum (15)
    0x29, 0x12,       //   Usage Maximum (18)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x34,       //   Report Count (52)
    0x81, 0x03,       //   Input (Const,Var,Abs)
    0x06, 0x00, 0xFF, //   Usage Page (Vendor Specific)
    0x85, 0x21,       //   Report ID (0x21) — Subcommand reply
    0x09, 0x01,       //   Usage (1)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x81, 0x03,       //   Input (Const,Var,Abs)
    0x85, 0x81,       //   Report ID (0x81) — USB command reply
    0x09, 0x02,       //   Usage (2)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x81, 0x03,       //   Input (Const,Var,Abs)
    0x85, 0x01,       //   Report ID (0x01) — Subcommand output
    0x09, 0x03,       //   Usage (3)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Const,Var,Abs,Volatile)
    0x85, 0x10,       //   Report ID (0x10) — Rumble only
    0x09, 0x04,       //   Usage (4)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Const,Var,Abs,Volatile)
    0x85, 0x80,       //   Report ID (0x80) — USB command
    0x09, 0x05,       //   Usage (5)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Const,Var,Abs,Volatile)
    0x85, 0x82,       //   Report ID (0x82) — USB command 2
    0x09, 0x06,       //   Usage (6)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0x91, 0x83,       //   Output (Const,Var,Abs,Volatile)
    0x85, 0xF6,       //   Report ID (0xF6) — Config set (DS5Dongle)
    0x09, 0x07,       //   Usage (7)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3F,       //   Report Count (63)
    0xB1, 0x02,       //   Feature (Data,Var,Abs)
    0x85, 0xF7,       //   Report ID (0xF7) — Config get (DS5Dongle)
    0x09, 0x08,       //   Usage (8)
    0xB1, 0x02,       //   Feature (Data,Var,Abs)
    0xC0,             // End Collection
};
static_assert(sizeof(hid_report_desc) == 219, "Pro Controller HID descriptor must be 219 bytes");

// ============================================================
// Configuration Descriptor — Pro Controller standalone (41 bytes)
// Single HID interface, EP1 IN + EP1 OUT, no audio
// ============================================================
static const uint8_t config_desc[] = {
    0x09,       // bLength
    0x02,       // bDescriptorType (CONFIGURATION)
    0x29, 0x00, // wTotalLength: 41
    0x01,       // bNumInterfaces
    0x01,       // bConfigurationValue
    0x00,       // iConfiguration
    0xA0,       // bmAttributes: bus powered, remote wakeup
    0xFA,       // bMaxPower: 500mA

    0x09,       // bLength
    0x04,       // bDescriptorType (INTERFACE)
    0x00,       // bInterfaceNumber
    0x00,       // bAlternateSetting
    0x02,       // bNumEndpoints
    0x03,       // bInterfaceClass: HID
    0x00,       // bInterfaceSubClass
    0x00,       // bInterfaceProtocol
    0x00,       // iInterface

    0x09,       // bLength
    0x21,       // bDescriptorType (HID)
    0x11, 0x01, // bcdHID
    0x00,       // bCountryCode
    0x01,       // bNumDescriptors
    0x22,       // bDescriptorType: Report
    static_cast<uint8_t>(sizeof(hid_report_desc) & 0xFF),
    static_cast<uint8_t>((sizeof(hid_report_desc) >> 8) & 0xFF),

    0x07,       // bLength
    0x05,       // bDescriptorType (ENDPOINT)
    0x81,       // bEndpointAddress: IN EP1
    0x03,       // bmAttributes: Interrupt
    0x40, 0x00, // wMaxPacketSize: 64
    0x08,       // bInterval: 8ms

    0x07,       // bLength
    0x05,       // bDescriptorType (ENDPOINT)
    0x01,       // bEndpointAddress: OUT EP1
    0x03,       // bmAttributes: Interrupt
    0x40, 0x00, // wMaxPacketSize: 64
    0x08,       // bInterval: 8ms
};
static_assert(sizeof(config_desc) == 0x29, "Pro Controller config descriptor must be 41 bytes");

// ============================================================
// State
// ============================================================
critical_section_t pro_cs;
bool pro_cs_ready = false;
uint32_t last_report_us = 0;
uint8_t pro_state[REPORT_SIZE]{};
uint8_t pending_report_id = 0;
uint8_t pending_report[REPORT_SIZE]{};
bool pending_report_ready = false;
bool usb_enabled = false;
bool imu_enabled = false;
uint8_t timer_counter = 0;
uint8_t rumble_seq = 0;
uint8_t mac_addr[6] = {0x98, 0xB6, 0xE9, 0x00, 0x00, 0x01};

// ============================================================
// Helpers
// ============================================================

uint16_t u8_to_u12(uint8_t value) {
    return static_cast<uint16_t>(static_cast<uint32_t>(value) * 4095u / 255u);
}

uint16_t u8_to_u12_inv(uint8_t value) {
    return static_cast<uint16_t>(4095u - u8_to_u12(value));
}

void encode_stick(uint8_t *data, uint8_t offset, uint16_t x, uint16_t y) {
    data[offset] = x & 0xFF;
    data[offset + 1] = static_cast<uint8_t>(((y & 0x0F) << 4) | ((x >> 8) & 0x0F));
    data[offset + 2] = static_cast<uint8_t>((y >> 4) & 0xFF);
}

void put_i16_le(uint8_t *dst, int16_t value) {
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
}

void put_u32_le(uint8_t *dst, uint32_t value) {
    dst[0] = value & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value >> 16) & 0xFF;
    dst[3] = (value >> 24) & 0xFF;
}

int16_t clamp_i16(int64_t v) {
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(v);
}

// ============================================================
// HD Rumble → DS5 rumble forwarding
// ============================================================

// Decode Switch HD Rumble high-band amplitude (byte[1] of a 4-byte motor pack)
uint8_t decode_hf_amp(uint8_t b) {
    uint8_t ha = b >> 1;  // 7-bit amplitude
    if (ha == 0) return 0;
    return static_cast<uint8_t>(std::min<uint16_t>(ha * 2, 255));
}

// Decode Switch HD Rumble low-band amplitude (byte[3] of a 4-byte motor pack)
uint8_t decode_lf_amp(uint8_t b) {
    if (b <= 0x40) return 0;
    return static_cast<uint8_t>(std::min<uint16_t>((b - 0x40) * 3, 255));
}

// Forward Switch HD Rumble to DS5 via BT
// rumble_data points to 8 bytes: [left_motor(4)] [right_motor(4)]
void forward_rumble(const uint8_t *rumble_data) {
    // Decode left motor → DS5 heavy (big) motor
    uint8_t left_hf  = decode_hf_amp(rumble_data[1]);
    uint8_t left_lf  = decode_lf_amp(rumble_data[3]);
    uint8_t heavy    = std::max(left_hf, left_lf);

    // Decode right motor → DS5 soft (small) motor
    uint8_t right_hf = decode_hf_amp(rumble_data[5]);
    uint8_t right_lf = decode_lf_amp(rumble_data[7]);
    uint8_t soft     = std::max(right_hf, right_lf);

    // Build DS5 BT output report (0x31)
    uint8_t out[78]{};
    out[0] = 0x31;                           // BT report ID
    out[1] = (rumble_seq++ & 0x0F) << 4;     // Sequence counter (4-bit)
    out[2] = 0x10;                           // Tag
    out[3] = 0x01 | 0x02;                    // valid_flag0: compat rumble + haptics
    out[4] = 0x00;                           // valid_flag1
    out[5] = soft;                           // Right motor (small)
    out[6] = heavy;                          // Left motor (big)
    bt_write(out, sizeof(out));
}

uint8_t dpad_to_bits(Direction dpad) {
    switch (dpad) {
        case North:     return 0x02;
        case NorthEast: return 0x02 | 0x04;
        case East:      return 0x04;
        case SouthEast: return 0x01 | 0x04;
        case South:     return 0x01;
        case SouthWest: return 0x01 | 0x08;
        case West:      return 0x08;
        case NorthWest: return 0x02 | 0x08;
        case None:
        default:        return 0x00;
    }
}

uint8_t battery_level(const USBGetStateData &ds5) {
    uint8_t battery = (ds5.Power == Complete) ? 8 :
                      (ds5.PowerPercent >= 8) ? 8 :
                      (ds5.PowerPercent >= 6) ? 6 :
                      (ds5.PowerPercent >= 3) ? 4 :
                      (ds5.PowerPercent >= 1) ? 2 : 0;
    bool charging = (ds5.Power == Charging || ds5.Power == Complete);
    if (charging) battery += 1;
    return static_cast<uint8_t>((battery << 4) | 0x01); // 0x01 = USB connected
}

// IMU conversion: DS5 accel → Switch accel
int16_t ds5_accel_to_switch(int16_t value) {
    return clamp_i16(static_cast<int32_t>(value) * SWITCH_ACCEL_RES_PER_G / DS5_ACCEL_RES_PER_G);
}

// IMU conversion: DS5 gyro → Switch gyro
int16_t ds5_gyro_to_switch(int16_t value) {
    int64_t dps_x1000 = static_cast<int32_t>(value) * 1000 / DS5_EFFECTIVE_GYRO_RES_PER_DEG_S;
    return clamp_i16(dps_x1000 * SWITCH_GYRO_RES_PER_DEG_S_X1000 / (1000 * 1000));
}

// ============================================================
// State snapshot
// ============================================================

void reset_state() {
    memset(pro_state, 0, sizeof(pro_state));
    pro_state[1] = 0x91; // battery: full, USB
    encode_stick(pro_state, 5, 2048, 2048);
    encode_stick(pro_state, 8, 2048, 2048);
    pending_report_ready = false;
    usb_enabled = false;
    imu_enabled = false;
    timer_counter = 0;
    last_report_us = 0;
}

void build_state_snapshot(uint8_t out[REPORT_SIZE]) {
    critical_section_enter_blocking(&pro_cs);
    memcpy(out, pro_state, REPORT_SIZE);
    critical_section_exit(&pro_cs);
    out[0] = timer_counter++;
}

void build_input_report(uint8_t out[REPORT_SIZE]) {
    critical_section_enter_blocking(&pro_cs);
    memcpy(out, pro_state, REPORT_SIZE);
    critical_section_exit(&pro_cs);
    out[0] = timer_counter;
    timer_counter += 3; // 3 IMU samples per report
}

// ============================================================
// Report queueing
// ============================================================

void queue_input(uint8_t report_id, const uint8_t *payload, uint16_t len) {
    memset(pending_report, 0, sizeof(pending_report));
    memcpy(pending_report, payload, std::min<uint16_t>(len, sizeof(pending_report)));
    pending_report_id = report_id;
    pending_report_ready = true;
}

void queue_proprietary_ack(uint8_t command) {
    uint8_t payload[REPORT_SIZE]{};
    payload[0] = command;
    if (command == 0x01) {
        payload[2] = DEVICE_TYPE;
        for (uint8_t i = 0; i < sizeof(mac_addr); ++i) {
            payload[3 + i] = mac_addr[sizeof(mac_addr) - 1 - i];
        }
    }
    queue_input(0x81, payload, sizeof(payload));
}

// ============================================================
// SPI Flash emulation
// ============================================================

void encode_left_cal(uint8_t *dst) {
    encode_stick(dst, 0, 1536, 1536); // max above center
    encode_stick(dst, 3, 2048, 2048); // center
    encode_stick(dst, 6, 1536, 1536); // min below center
}

void encode_right_cal(uint8_t *dst) {
    encode_stick(dst, 0, 2048, 2048); // center
    encode_stick(dst, 3, 1536, 1536); // min below center
    encode_stick(dst, 6, 1536, 1536); // max above center
}

void build_spi_read(uint32_t address, uint8_t length, uint8_t *dst, uint8_t dst_len) {
    memset(dst, 0, dst_len);

    static const uint8_t color_flag[] = {0x01};
    static const uint8_t controller_color[] = {
        0xF4, 0xF4, 0xF4, // body
        0x28, 0x2D, 0x33, // buttons
        0x1F, 0x22, 0x26, // left grip
        0x1F, 0x22, 0x26, // right grip
        0xFF
    };

    // Color flag at 0x601B
    if (address == 0x601B && dst_len >= 1) {
        memcpy(dst, color_flag, std::min<uint8_t>(dst_len, sizeof(color_flag)));
        return;
    }
    // Colors at 0x6050
    if (address >= 0x6050 && address < 0x6050 + sizeof(controller_color)) {
        uint32_t off = address - 0x6050;
        uint8_t clen = std::min<uint8_t>(dst_len, sizeof(controller_color) - off);
        memcpy(dst, controller_color + off, clen);
        return;
    }
    // Stick calibration at 0x603D
    if (address == 0x603D) {
        encode_left_cal(dst);
        if (dst_len >= 18) encode_right_cal(dst + 9);
        if (dst_len >= 25) {
            dst[18] = 0x32; dst[19] = 0x32; dst[20] = 0x32;
            memset(dst + 21, 0xFF, 4);
        }
        return;
    }
    // Serial at 0x6000
    if (address == 0x6000) {
        memset(dst, 0xFF, dst_len);
        return;
    }
    // Factory sensor + stick params at 0x6080
    if (address == 0x6080) {
        static const uint8_t factory_params[] = {
            0x50, 0xFD, 0x00, 0x00, 0xC6, 0x0F, 0x0F, 0x30,
            0x61, 0x96, 0x30, 0xF3, 0xD4, 0x14, 0x54, 0x41,
            0x15, 0x54, 0xC7, 0x79, 0x9C, 0x33, 0x36, 0x63
        };
        memcpy(dst, factory_params, std::min<uint8_t>(dst_len, sizeof(factory_params)));
        return;
    }
    // Stick params 2 at 0x6098
    if (address == 0x6098) {
        static const uint8_t params2[] = {
            0x0F, 0x30, 0x61, 0x96, 0x30, 0xF3, 0xD4, 0x14,
            0x54, 0x41, 0x15, 0x54, 0xC7, 0x79, 0x9C, 0x33,
            0x36, 0x63
        };
        memcpy(dst, params2, std::min<uint8_t>(dst_len, sizeof(params2)));
        return;
    }
    // Factory IMU cal at 0x6020
    if (address == 0x6020 && dst_len >= 24) {
        // Zero bias, standard coefficients
        memset(dst, 0, 6);  // accel bias
        dst[6] = 0x00; dst[7] = 0x40; // accel coeff X
        dst[8] = 0x00; dst[9] = 0x40; // accel coeff Y
        dst[10] = 0x00; dst[11] = 0x40; // accel coeff Z
        memset(dst + 12, 0, 6); // gyro offset
        dst[18] = 0x3A; dst[19] = 0x34; // gyro coeff X
        dst[20] = 0x3A; dst[21] = 0x34; // gyro coeff Y
        dst[22] = 0x3A; dst[23] = 0x34; // gyro coeff Z
        return;
    }
    // User cal — not set
    if (address == 0x8010 || address == 0x8026 || address == 0x8028) {
        memset(dst, 0xFF, dst_len);
        return;
    }
}

// ============================================================
// Subcommand handler
// ============================================================

void queue_subcommand_reply(uint8_t subcommand, const uint8_t *request_data, uint16_t request_len) {
    uint8_t payload[REPORT_SIZE]{};
    build_state_snapshot(payload);
    payload[12] = 0x80;
    payload[13] = subcommand;

    switch (subcommand) {
        case 0x01: // Pairing
            payload[12] = 0x81;
            payload[14] = DEVICE_TYPE;
            break;

        case 0x02: // Device info
            payload[12] = 0x82;
            payload[14] = 0x03; // FW ver
            payload[15] = 0x48;
            payload[16] = DEVICE_TYPE;
            payload[17] = 0x02;
            memcpy(payload + 18, mac_addr, sizeof(mac_addr));
            payload[24] = 0x03;
            payload[25] = 0x01;
            printf("[SwitchPro] Subcmd 0x02: Device Info\n");
            break;

        case 0x03: // Set input report mode
            if (request_len >= 1 && (request_data[0] == 0x30 || request_data[0] == 0x3F)) {
                usb_enabled = true;
            }
            printf("[SwitchPro] Subcmd 0x03: Mode=0x%02X\n", request_len >= 1 ? request_data[0] : 0);
            break;

        case 0x04: // Trigger buttons elapsed time
            payload[12] = 0x83;
            break;

        case 0x08: // Shipment
            break;

        case 0x10: // SPI Flash Read
            payload[12] = 0x90;
            if (request_len >= 5) {
                uint32_t addr = static_cast<uint32_t>(request_data[0]) |
                    (static_cast<uint32_t>(request_data[1]) << 8) |
                    (static_cast<uint32_t>(request_data[2]) << 16) |
                    (static_cast<uint32_t>(request_data[3]) << 24);
                uint8_t len = std::min<uint8_t>(request_data[4], 29);
                put_u32_le(payload + 14, addr);
                payload[18] = len;
                build_spi_read(addr, len, payload + 19, len);
                printf("[SwitchPro] Subcmd 0x10: SPI Read 0x%04X len=%d\n", (unsigned)addr, len);
            }
            break;

        case 0x30: // Player LEDs
            printf("[SwitchPro] Subcmd 0x30: Player LEDs\n");
            break;

        case 0x38: // HOME Light
            break;

        case 0x40: // Enable IMU
            if (request_len >= 1) {
                critical_section_enter_blocking(&pro_cs);
                imu_enabled = request_data[0] != 0;
                critical_section_exit(&pro_cs);
                printf("[SwitchPro] Subcmd 0x40: IMU=%d\n", imu_enabled);
            }
            break;

        case 0x41: // IMU sensitivity
            break;

        case 0x48: // Enable vibration
            break;

        case 0x21: // NFC MCU config
            payload[12] = 0xA0;
            payload[14] = 0x01;
            payload[15] = 0x00;
            payload[16] = 0xFF;
            payload[17] = 0x00;
            payload[18] = 0x03;
            payload[19] = 0x00;
            payload[20] = 0x05;
            payload[21] = 0x01;
            break;

        default:
            printf("[SwitchPro] Subcmd 0x%02X: unhandled\n", subcommand);
            break;
    }

    queue_input(0x21, payload, sizeof(payload));
}

// ============================================================
// Report sending
// ============================================================

void drain_pending() {
    if (!pending_report_ready || !tud_hid_ready()) return;
    tud_hid_report(pending_report_id, pending_report, sizeof(pending_report));
    pending_report_ready = false;
}

void send_state_if_due() {
    uint32_t now = time_us_32();
    if (!usb_enabled || !tud_hid_ready()) return;
    if (last_report_us != 0 &&
        static_cast<uint32_t>(now - last_report_us) < REPORT_INTERVAL_US) {
        return;
    }

    uint8_t state[REPORT_SIZE]{};
    build_input_report(state);

    if (tud_hid_report(0x30, state, sizeof(state))) {
        last_report_us = now;
    }
}

} // namespace

// ============================================================
// Public API
// ============================================================

void switch_pro_init() {
    if (!pro_cs_ready) {
        critical_section_init(&pro_cs);
        pro_cs_ready = true;
    }
    critical_section_enter_blocking(&pro_cs);
    reset_state();
    critical_section_exit(&pro_cs);
    printf("[SwitchPro] Initialized\n");
}

void switch_pro_on_ds5_input(const USBGetStateData &ds5) {
    uint8_t state[REPORT_SIZE]{};
    state[0] = 0x00; // timer placeholder
    state[1] = battery_level(ds5);

    // Buttons byte 2 (right): Y(0) X(1) B(2) A(3) SR(4) SL(5) R(6) ZR(7)
    if (ds5.ButtonSquare)   state[2] |= 0x01; // Y
    if (ds5.ButtonTriangle) state[2] |= 0x02; // X
    if (ds5.ButtonCross)    state[2] |= 0x04; // B
    if (ds5.ButtonCircle)   state[2] |= 0x08; // A
    if (ds5.ButtonR1)       state[2] |= 0x40; // R
    if (ds5.ButtonR2 || ds5.TriggerRight > DS5_TRIGGER_THRESHOLD)
                            state[2] |= 0x80; // ZR

    // Buttons byte 3 (shared): -(0) +(1) RS(2) LS(3) Home(4) Capture(5)
    if (ds5.ButtonCreate)   state[3] |= 0x01; // Minus
    if (ds5.ButtonOptions)  state[3] |= 0x02; // Plus
    if (ds5.ButtonR3)       state[3] |= 0x04; // RS
    if (ds5.ButtonL3)       state[3] |= 0x08; // LS
    if (ds5.ButtonHome)     state[3] |= 0x10; // Home
    if (ds5.ButtonPad)      state[3] |= 0x20; // Capture

    // Buttons byte 4 (left): Down(0) Up(1) Right(2) Left(3) SR(4) SL(5) L(6) ZL(7)
    state[4] |= dpad_to_bits(ds5.DPad);
    if (ds5.ButtonL1)       state[4] |= 0x40; // L
    if (ds5.ButtonL2 || ds5.TriggerLeft > DS5_TRIGGER_THRESHOLD)
                            state[4] |= 0x80; // ZL

    // Sticks (12-bit packed)
    encode_stick(state, 5, u8_to_u12(ds5.LeftStickX), u8_to_u12_inv(ds5.LeftStickY));
    encode_stick(state, 8, u8_to_u12(ds5.RightStickX), u8_to_u12_inv(ds5.RightStickY));

    // IMU: 3 samples × 12 bytes starting at offset 12
    if (imu_enabled) {
        // Switch IMU order: accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z
        // DS5→Switch axis mapping (from SDL):
        // Switch_X = -DS5_Z(accel), Switch_Y = -DS5_X(accel), Switch_Z = DS5_Y(accel)
        // Switch_X = -DS5_Z(gyro),  Switch_Y = -DS5_X(gyro),  Switch_Z = DS5_Y(gyro)
        int16_t ax = clamp_i16(-ds5_accel_to_switch(ds5.AccelerometerZ));
        int16_t ay = clamp_i16(-ds5_accel_to_switch(ds5.AccelerometerX));
        int16_t az = ds5_accel_to_switch(ds5.AccelerometerY);
        int16_t gx = clamp_i16(-ds5_gyro_to_switch(ds5.AngularVelocityY));
        int16_t gy = clamp_i16(-ds5_gyro_to_switch(ds5.AngularVelocityX));
        int16_t gz = ds5_gyro_to_switch(ds5.AngularVelocityZ);

        for (int i = 0; i < 3; i++) {
            uint8_t *sample = state + 12 + i * 12;
            put_i16_le(sample + 0, ax);
            put_i16_le(sample + 2, ay);
            put_i16_le(sample + 4, az);
            put_i16_le(sample + 6, gx);
            put_i16_le(sample + 8, gy);
            put_i16_le(sample + 10, gz);
        }
    }

    critical_section_enter_blocking(&pro_cs);
    memcpy(pro_state, state, sizeof(pro_state));
    critical_section_exit(&pro_cs);
}

void switch_pro_task() {
    if (!is_switch_mode()) return;
    drain_pending();
    send_state_if_due();
}

uint16_t switch_pro_get_report(uint8_t report_id, uint8_t *buffer, uint16_t reqlen) {
    if (!is_switch_mode()) return 0;

    // DS5Dongle config feature reports
    if (report_id == 0xF7) {
        return pico_cmd_get(report_id, buffer, reqlen);
    }

    uint8_t payload[REPORT_SIZE]{};
    uint16_t len = sizeof(payload);

    switch (report_id) {
        case 0x30:
            build_input_report(payload);
            break;
        case 0x81:
            payload[0] = 0x01;
            payload[2] = DEVICE_TYPE;
            for (uint8_t i = 0; i < sizeof(mac_addr); ++i) {
                payload[3 + i] = mac_addr[sizeof(mac_addr) - 1 - i];
            }
            break;
        default:
            return 0;
    }

    len = std::min<uint16_t>(len, reqlen);
    memcpy(buffer, payload, len);
    return len;
}

void switch_pro_handle_hid_out(uint8_t report_id, uint8_t const *buffer, uint16_t len) {
    if (!is_switch_mode() || len == 0) return;

    uint8_t actual_id = report_id;
    const uint8_t *payload = buffer;
    uint16_t payload_len = len;

    // TinyUSB may pass report_id=0 with actual ID in buffer[0]
    if (actual_id == 0) {
        actual_id = buffer[0];
        payload = buffer + 1;
        payload_len = len - 1;
        if (payload_len == 0) return;
    }

    // DS5Dongle config
    if (actual_id == 0xF6) {
        pico_cmd_set(0xF6, payload, payload_len);
        return;
    }

    // USB commands (0x80)
    if (actual_id == 0x80) {
        switch (payload[0]) {
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x06:
                queue_proprietary_ack(payload[0]);
                printf("[SwitchPro] USB CMD 0x%02X\n", payload[0]);
                break;
            case 0x04:
                usb_enabled = true;
                printf("[SwitchPro] USB CMD 0x04: USB enabled\n");
                break;
            case 0x05:
                usb_enabled = false;
                queue_proprietary_ack(payload[0]);
                break;
            default:
                break;
        }
        return;
    }

    // Subcommand (0x01): rumble[8] + subcmd_id + args
    if (actual_id == 0x01 && payload_len >= 10) {
        // payload[0] = counter, [1..8] = rumble, [9] = subcmd, [10..] = args
        forward_rumble(payload + 1);
        queue_subcommand_reply(payload[9], payload + 10, payload_len - 10);
        return;
    }

    // Rumble only (0x10)
    if (actual_id == 0x10 && payload_len >= 9) {
        // payload[0] = counter, [1..8] = rumble data
        forward_rumble(payload + 1);
        return;
    }
}

const uint8_t *get_switch_pro_hid_report_desc() {
    return hid_report_desc;
}

uint16_t get_switch_pro_hid_report_desc_len() {
    return sizeof(hid_report_desc);
}

const uint8_t *get_switch_pro_config_desc() {
    return config_desc;
}

uint16_t get_switch_pro_config_desc_len() {
    return sizeof(config_desc);
}
