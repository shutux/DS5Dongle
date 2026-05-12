# DS5Dongle Build Notes

- SDK: Pico SDK 2.2.0
- **TinyUSB: must use 0.20.0** (SDK ships 0.18.0, which is incompatible)
- Switch TinyUSB: `git -C "$PICO_SDK_PATH/lib/tinyusb" checkout --detach 0.20.0`
- See `.github/workflows/build.yml` for CI config
- v0.5.4 code compiles cleanly with TinyUSB 0.20.0, no tusb_config.h changes needed
- TinyUSB 0.18.0 has UAC2-only audio driver, breaks DS5's UAC1 descriptors
- Board: pico2_w (RP2350 + CYW43439)
- Toolchain: arm-none-eabi-gcc 14.2 Rel1
- Build command: `cd C:\PeaSyo\DS5Dongle; cmake --build build`
- Output: `build/ds5-bridge.uf2`

## Status (2026-05-12)
- **DS5 mode (PC/PS5): WORKING** ✓
- **DSE mode (PC): WORKING** ✓
- **Switch Pro mode: WORKING** ✓ (with gyro/IMU)
- **Audio/Haptics: WORKING** ✓
- **Combo mode switching: WORKING** ✓ (PS+Options+D-pad, 3s hold)
- Upstream: `https://github.com/awalol/DS5Dongle.git`
- Our fork: `https://github.com/shutux/DS5Dongle.git`

## CMake Options
| Option | Default | Description |
|--------|---------|-------------|
| ENABLE_SERIAL | OFF | USB CDC debug serial |
| ENABLE_VERBOSE | OFF | Verbose BT logging |
| ENABLE_BATT_LED | ON | Low-battery LED blink |
| ENABLE_WAKE_HID | OFF | Windows PS-button wake |
| WAKE_DEBUG | OFF | Wake FSM UART tracing |

## Key Lessons
- TinyUSB class drivers init in fixed order (Audio→HID), can't change interface layout per mode
- `GAP_EVENT_INQUIRY_COMPLETE` and `HCI_EVENT_INQUIRY_COMPLETE` must both handle inquiry restart or use fall-through
- ENABLE_SERIAL needs IAD composite device support; TUD_CDC_DESCRIPTOR macro already includes its own 8-byte IAD
- Config stored in last flash sector (4KB), magic `0x66ccff00`
- Pico overclocked to 320MHz (vreg 1.20V)
