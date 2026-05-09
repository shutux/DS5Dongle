//
// Platform auto-detection
//
// Strategy:
//   1. BT connects → enumerate USB as Switch Pro Controller
//   2. If Switch sends 0x80 handshake within 1.5s → stay as Switch Pro
//   3. If timeout → disconnect USB, wait 200ms, reconnect as DS5/DSE
//   4. config controller_mode selects DS5(0) vs DSE(1) fallback
//      config controller_mode=2 forces Switch Pro (no fallback)
//

#include "platform.h"
#include "config.h"
#include "pico/time.h"
#include "device/usbd.h"
#include <cstdio>

static constexpr uint32_t SWITCH_DETECT_TIMEOUT_MS = 3000;
static constexpr uint32_t USB_RECONNECT_DELAY_MS   = 200;

static DetectState g_detect_state = DETECT_IDLE;
static PlatformMode g_active_mode = PLATFORM_DS5;
static absolute_time_t g_detect_deadline;
static absolute_time_t g_reconnect_time;

PlatformMode get_platform_mode() {
    return g_active_mode;
}

bool is_switch_mode() {
    return g_active_mode == PLATFORM_SWITCH_PRO;
}

bool is_ds5_mode() {
    return g_active_mode == PLATFORM_DS5 || g_active_mode == PLATFORM_DSE;
}

void platform_detect_start() {
    uint8_t cfg_mode = get_config().controller_mode;
    if (cfg_mode == 2) {
        g_active_mode = PLATFORM_SWITCH_PRO;
        g_detect_state = DETECT_CONFIRMED;
        printf("[Platform] Config: Switch mode\n");
    } else if (cfg_mode == 1) {
        g_active_mode = PLATFORM_DSE;
        g_detect_state = DETECT_CONFIRMED;
        printf("[Platform] Config: DSE mode\n");
    } else {
        g_active_mode = PLATFORM_DS5;
        g_detect_state = DETECT_CONFIRMED;
        printf("[Platform] Config: DS5 mode\n");
    }
}

bool platform_detect_tick() {
    if (g_detect_state == DETECT_TRYING_SWITCH) {
        if (absolute_time_diff_us(get_absolute_time(), g_detect_deadline) <= 0) {
            // Timeout — no Switch handshake, switch to DS5/DSE
            uint8_t cfg_mode = get_config().controller_mode;
            g_active_mode = (cfg_mode == 1) ? PLATFORM_DSE : PLATFORM_DS5;

            printf("[Platform] No Switch handshake, switching to %s\n",
                   g_active_mode == PLATFORM_DSE ? "DSE" : "DS5");

            tud_disconnect();
            g_detect_state = DETECT_NEED_RECONNECT;
            g_reconnect_time = make_timeout_time_ms(USB_RECONNECT_DELAY_MS);
            return false;
        }
    }

    if (g_detect_state == DETECT_NEED_RECONNECT) {
        if (absolute_time_diff_us(get_absolute_time(), g_reconnect_time) <= 0) {
            printf("[Platform] Reconnecting USB as %s\n",
                   g_active_mode == PLATFORM_DSE ? "DSE" : "DS5");
            tud_connect();
            g_detect_state = DETECT_CONFIRMED;
            return true;
        }
    }

    return false;
}

void platform_detect_switch_confirmed() {
    if (g_detect_state == DETECT_TRYING_SWITCH) {
        g_detect_state = DETECT_CONFIRMED;
        g_active_mode = PLATFORM_SWITCH_PRO;
        printf("[Platform] Switch handshake received — confirmed Switch Pro\n");
    }
}

void platform_detect_reset() {
    g_detect_state = DETECT_IDLE;
    g_active_mode = PLATFORM_DS5;
    printf("[Platform] Reset\n");
}
