//
// DS5 combo key detection for mode switching
//
// PS + Create + Cross(×)  → DS5 mode (controller_mode=0)
// PS + Create + Circle(○) → DSE mode (controller_mode=1)
// PS + Create + Square(□) → Switch mode (controller_mode=2)
//
// Hold for 3 seconds to trigger. Saves to flash and reconnects USB.
//

#include "combo.h"
#include "config.h"
#include "platform.h"
#include "device/usbd.h"
#include "pico/time.h"
#include <cstdio>

// DS5 report byte layout:
// [7] bit4=Square, bit5=Cross, bit6=Circle, bit7=Triangle
// [8] bit4=Create
// [9] bit0=PS

static constexpr uint32_t COMBO_HOLD_MS = 3000;

static uint8_t combo_target = 0xFF;  // 0xFF = no combo active
static absolute_time_t combo_start;

static uint8_t detect_combo(const uint8_t *ds5) {
    uint8_t btn0 = ds5[7];
    uint8_t btn1 = ds5[8];
    uint8_t btn2 = ds5[9];

    bool ps     = btn2 & 0x01;
    bool create = btn1 & 0x10;

    if (!ps || !create) {
        return 0xFF;
    }

    if (btn0 & 0x20) return 0; // Cross → DS5
    if (btn0 & 0x40) return 1; // Circle → DSE
    if (btn0 & 0x10) return 2; // Square → Switch

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

    // Reconnect USB with new mode
    platform_detect_start();
    tud_disconnect();
    sleep_ms(1000);
    tud_connect();

    combo_target = 0xFF;
    return true;
}
