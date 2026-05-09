//
// DS5 combo key detection for mode switching
//
// PS + Options + D-pad Up    → DS5 mode (controller_mode=0)
// PS + Options + D-pad Right → DSE mode (controller_mode=1)
// PS + Options + D-pad Left  → Switch mode (controller_mode=2)
//
// Hold for 3 seconds to trigger. Saves to flash and reconnects USB.
//

#include "combo.h"
#include "config.h"
#include "platform.h"
#include "bt.h"
#include "device/usbd.h"
#include "pico/time.h"
#include <cstdio>

// DS5 report byte layout:
// [7] bits0-3=D-pad (hat: 0=Up,1=NE,2=Right,3=SE,4=Down,5=SW,6=Left,7=NW,8=None)
// [8] bit5=Options
// [9] bit0=PS

static constexpr uint32_t COMBO_HOLD_MS = 3000;

static uint8_t combo_target = 0xFF;  // 0xFF = no combo active
static absolute_time_t combo_start;

static uint8_t detect_combo(const uint8_t *ds5) {
    uint8_t btn0 = ds5[7];
    uint8_t btn1 = ds5[8];
    uint8_t btn2 = ds5[9];

    bool ps      = btn2 & 0x01;
    bool options = btn1 & 0x20;

    if (!ps || !options) {
        return 0xFF;
    }

    uint8_t dpad = btn0 & 0x0F;
    if (dpad == 0) return 0; // D-pad Up    → DS5
    if (dpad == 2) return 1; // D-pad Right → DSE
    if (dpad == 6) return 2; // D-pad Left  → Switch

    return 0xFF;
}

bool combo_check(const uint8_t *ds5_report) {
    uint8_t target = detect_combo(ds5_report);

    if (target == 0xFF || target == get_config().controller_mode) {
        // No combo or same mode — reset
        combo_target = 0xFF;
        return false;
    }

    if (target != combo_target) {
        // New combo started
        combo_target = target;
        combo_start = get_absolute_time();
        printf("[Combo] Holding for mode %d...\n", target);
        return false;
    }

    // Same combo held — check duration
    int64_t elapsed = absolute_time_diff_us(combo_start, get_absolute_time());
    if (elapsed < (int64_t)COMBO_HOLD_MS * 1000) {
        return false;
    }

    // Triggered!
    printf("[Combo] Mode switch to %d triggered!\n", target);

    // Update config
    Config_body body = get_config();
    body.controller_mode = target;
    set_config(reinterpret_cast<const uint8_t *>(&body), sizeof(body));
    config_save();

    // Disconnect BT so DS5 immediately goes to sleep
    bt_disconnect();

    // Reconnect USB with new mode
    platform_detect_start();
    tud_disconnect();
    sleep_ms(1000);
    tud_connect();

    combo_target = 0xFF;
    return true;
}
