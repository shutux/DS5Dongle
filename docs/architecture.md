# DS5Dongle — Architecture & Data Flow

## Startup Sequence
```
vreg 1.20V + 320MHz overclock
  → board_init → tusb_init(Full Speed)
  → tud_disconnect (non-serial only, 150ms delay)
  → cyw43_arch_init (BT/WiFi chip)
  → [battery_led_init]
  → watchdog reboot check
  → critical_section_init(&report_cs)
  → wake_init
  → config_load (flash last sector)
  → platform_detect_start
  → switch_pro_init
  → bt_init + bt_register_data_callback(on_bt_data)
  → audio_init (Core 1 Opus encoder)
  → [watchdog_enable(1000ms)]
  → main loop
```

## Main Loop
```c
while(1) {
    watchdog_update();
    cyw43_arch_poll();      // BT/WiFi event processing (BTstack)
    tud_task();             // USB device events (TinyUSB)
    wake_task();            // Wake state machine

    platform_detect_tick(); // Platform detection timeout
    combo_check();          // Mode switch combo (PS+Options+D-pad 3s)

    if (switch_mode) {
        switch_pro_task();  // Timed Switch Pro report sending (66.7Hz)
    } else {
        audio_loop();       // Audio buffer → BT
        interrupt_loop();   // HID input → USB host
    }
    [battery_led_tick();]
}
```

## Data Flow: DualSense → Host (Input)
```
DS5 BT → L2CAP Interrupt (0xA1 0x31 <77B>)
  ↓
l2cap_packet_handler → on_bt_data(channel, data, len)
  ↓
  ├─ [Switch Mode]:
  │   memcpy to USBGetStateData → switch_pro_on_ds5_input()
  │   → button/stick/IMU conversion → pro_state[63]
  │   → switch_pro_task() timer → tud_hid_report(0x30, pro_state, 63)
  │
  └─ [DS5/DSE Mode]:
      memcpy to interrupt_in_data[63]
      → interrupt_loop() → tud_hid_report(0x01, data, 63)
```

## Data Flow: Host → DualSense (Output)
```
USB Host → tud_hid_set_report_cb(report_id, buffer, bufsize)
  ↓
  ├─ [report_id=0, buffer[0]=0x02]:
  │   set_state_data(buffer+1, bufsize-1)
  │   → Build BT output (0xA2 0x31 + seq + tag + data + CRC32)
  │   → bt_write() → send_fifo → L2CAP
  │
  ├─ [Switch mode, 0x80/0x01/0x10]:
  │   switch_pro_handle_hid_out()
  │   ├─ 0x80: USB handshake → queue_proprietary_ack()
  │   ├─ 0x01: rumble + subcommand → forward_rumble() + reply
  │   └─ 0x10: rumble only → forward_rumble() → bt_write()
  │
  └─ [Feature 0x80/0x60-0x65/0xF0]:
      set_feature_data() → l2cap control channel (0x53 + id + data + CRC)
```

## Platform Detection (platform.cpp)
- State machine: `DETECT_NOT_STARTED` → `DETECT_STARTED` → `DETECT_CONFIRMED`
- Checks `config.controller_mode` to decide USB identity
- On mode switch: `platform_detect_start()` → `tud_disconnect()` → `tud_connect()`

## Config System
```c
struct Config_body {  // Packed, stored in flash
    float haptics_gain;          // [1.0, 2.0]
    float speaker_volume;        // [-100, 0] dB
    uint8_t inactive_time;       // [5, 60] minutes
    uint8_t disable_inactive_disconnect;
    uint8_t disable_pico_led;
    uint8_t polling_rate_mode;   // 0:250Hz, 1:500Hz, 2:real-time
    uint8_t audio_buffer_length; // [16, 128]
    uint8_t controller_mode;     // 0:DS5, 1:DSE, 2:Switch Pro
};
```
- Flash: last sector (4KB), magic `0x66ccff00`
- Read/Write via Feature Report 0xF6/0xF7
- 0xF6 subcmds: 0x01=update RAM, 0x02=save flash, 0x03=USB reconnect

## Combo Mode Switch
- **PS + Options + D-pad Up** → DS5 (mode 0)
- **PS + Options + D-pad Right** → DSE (mode 1)
- **PS + Options + D-pad Left** → Switch Pro (mode 2)
- Hold 3 seconds to trigger
- Saves to flash → bt_disconnect() → platform_detect_start() → USB reconnect

## File Map
| File | Purpose |
|------|---------|
| main.cpp | Entry point, main loop, USB callbacks |
| bt.cpp/h | Bluetooth HCI/L2CAP, DualSense BT protocol |
| switch_pro.cpp/h | Switch Pro Controller emulation |
| usb_descriptors.cpp | USB descriptors (DS5/DSE/Switch), dynamic switching |
| config.cpp/h | Flash config storage, validation |
| platform.cpp/h | Platform detection state machine |
| combo.cpp/h | Combo key mode switching |
| cmd.cpp/h | Custom HID command handler (0xF6/0xF7) |
| audio.cpp/h | USB Audio → Opus encode → BT audio report |
| wake.cpp/h | Windows S3 wake-on-PS-button |
| battery_led.cpp/h | Low battery LED indicator |
| ps5_auth.h | PS5 authentication passthrough |
