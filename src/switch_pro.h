//
// Switch Pro Controller USB emulation (057E:2009)
// Full protocol: handshake, subcommands, SPI flash, IMU, HD Rumble
//

#ifndef DS5_BRIDGE_SWITCH_PRO_H
#define DS5_BRIDGE_SWITCH_PRO_H

#include <cstdint>
#include "utils.h"

// --- Pro Controller identifiers ---
#define SWITCH_PRO_VID  0x057E
#define SWITCH_PRO_PID  0x2009

// Initialize Pro Controller state
void switch_pro_init();

// Called from BT callback when DS5 data arrives
void switch_pro_on_ds5_input(const USBGetStateData &ds5);

// Periodic task — drains pending reports and sends timed 0x30 input reports
void switch_pro_task();

// Handle GET_REPORT from host (returns length, 0 = stall)
uint16_t switch_pro_get_report(uint8_t report_id, uint8_t *buffer, uint16_t reqlen);

// Handle SET_REPORT / OUT endpoint data from host
void switch_pro_handle_hid_out(uint8_t report_id, uint8_t const *buffer, uint16_t len);

// Get Pro Controller HID report descriptor (219 bytes, with Report IDs)
const uint8_t *get_switch_pro_hid_report_desc();
uint16_t get_switch_pro_hid_report_desc_len();

// Get Pro Controller configuration descriptor (standalone, no audio)
const uint8_t *get_switch_pro_config_desc();
uint16_t get_switch_pro_config_desc_len();

#endif // DS5_BRIDGE_SWITCH_PRO_H
