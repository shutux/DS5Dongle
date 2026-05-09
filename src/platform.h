//
// Platform auto-detection and mode management
//

#ifndef DS5_BRIDGE_PLATFORM_H
#define DS5_BRIDGE_PLATFORM_H

#include <cstdint>

// Platform modes the dongle can operate in
enum PlatformMode : uint8_t {
    PLATFORM_DS5        = 0,  // PC/Android/PS5: appear as DualSense
    PLATFORM_DSE        = 1,  // PC/Android: appear as DualSense Edge
    PLATFORM_SWITCH_PRO = 2,  // Switch: appear as Switch Pro Controller
};

// Auto-detection state
enum DetectState : uint8_t {
    DETECT_IDLE,
    DETECT_TRYING_SWITCH,
    DETECT_CONFIRMED,
    DETECT_NEED_RECONNECT,  // USB disconnected, waiting to reconnect as DS5
};

// Get the active platform mode
PlatformMode get_platform_mode();

// Whether the current mode is Switch Pro
bool is_switch_mode();

// Whether the current mode is DS5/DSE (Sony)
bool is_ds5_mode();

// --- Auto-detection API ---

// Start detection: enumerate as Switch Pro first, fallback to DS5/DSE on timeout
void platform_detect_start();

// Called from main loop — handles timeout and USB reconnect
// Returns true if a USB reconnect happened this tick
bool platform_detect_tick();

// Called when Switch sends 0x80 handshake — confirms Switch mode
void platform_detect_switch_confirmed();

// Reset state (on BT disconnect)
void platform_detect_reset();

#endif // DS5_BRIDGE_PLATFORM_H
