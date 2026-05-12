# DS5Dongle — DualSense Protocol Reference

## USB Identifiers
| Mode | VID | PID |
|------|-----|-----|
| DS5 | 0x054C | 0x0CE6 |
| DualSense Edge | 0x054C | 0x0DF2 |

## HID Report IDs
| ID | Dir | Size | Description |
|----|-----|------|-------------|
| 0x01 | IN | 63B | Input Report (controller state) |
| 0x02 | OUT | 47B | Output Report (rumble/LED/triggers) |
| 0x05 | Feature | 40B | Calibration data |
| 0x08 | Feature | 47B | |
| 0x09 | Feature | 19B | |
| 0x20 | Feature | 63B | |
| 0x80-0x85 | Feature | various | PS5 authentication |
| 0xF0 | Feature SET | 63B | PS5 Auth nonce |
| 0xF1 | Feature GET | 63B | PS5 Auth signing state |
| 0xF2 | Feature GET | 15B | PS5 Auth signed response |
| **0xF6** | Feature SET | 63B | DS5Dongle config write |
| **0xF7** | Feature GET | 63B | DS5Dongle config read |

## USB Input Report (0x01) — 63 bytes: USBGetStateData
| Offset | Field |
|--------|-------|
| 0 | LeftStickX (0-255) |
| 1 | LeftStickY (0-255) |
| 2 | RightStickX (0-255) |
| 3 | RightStickY (0-255) |
| 4 | TriggerLeft (0-255) |
| 5 | TriggerRight (0-255) |
| 6 | SeqNo |
| 7[3:0] | D-Pad (0=N,1=NE,2=E,3=SE,4=S,5=SW,6=W,7=NW,8=None) |
| 7[4] | Square |
| 7[5] | Cross |
| 7[6] | Circle |
| 7[7] | Triangle |
| 8[0] | L1 |
| 8[1] | R1 |
| 8[2] | L2 |
| 8[3] | R2 |
| 8[4] | Create |
| 8[5] | Options |
| 8[6] | L3 |
| 8[7] | R3 |
| 9[0] | PS Home |
| 9[1] | Touchpad |
| 9[2] | Mute |
| 15-20 | Gyroscope (X/Z/Y int16) |
| 21-26 | Accelerometer (X/Y/Z int16) |
| 27-30 | SensorTimestamp (uint32) |
| 32-40 | TouchData (2 points + timestamp) |
| 52[3:0] | PowerPercent |
| 52[7:4] | PowerState |
| 53 | Connection flags (headset/mic/USB) |

## USB Output Report (0x02) — SetStateData
- Sent via `tud_hid_set_report_cb` report_id=0, buffer[0]=0x02
- Contains: rumble motors, LED color/mode, trigger effects, audio config
- Forwarded to DS5 via BT output report

## BT Report Wrappers
- **BT Input**: `0xA1 0x31 <77B>` — USBGetStateData starts at byte 3
- **BT Output**: `0xA2 0x31 <seq_hi4> <tag=0x10> <SetStateData>` + CRC32 (4 bytes)
- **BT Audio (0x36)**: 398 bytes total:
  - [13-75]: SetStateData (63B, LED/rumble/triggers)
  - [78-141]: Haptics (64 × int8, 2ch interleaved, 3kHz resampled from 48kHz ch3+ch4)
  - [144-343]: Opus audio (200B, 48kHz stereo, 10ms frame from ch1+ch2)

## Audio Pipeline
```
USB Audio IN (4ch, 48kHz, int16, Isochronous EP1 OUT 392B)
  ├─ ch1+ch2 → audio_fifo → Core1: resample 51200→48000 → Opus encode → opus_buf[200]
  └─ ch3+ch4 → WDL resample 48000→3000 → int8 → haptic_buf[64]
        ↓
Combined into BT report 0x36 and sent to DS5
```

## USB Endpoints (DS5 mode)
| EP | Dir | Type | Size | Use |
|----|-----|------|------|-----|
| EP1 | OUT | Isoch Adaptive | 392B | Audio Stream (4ch 16bit 48kHz) |
| EP2 | IN | Isoch Async | 196B | Mic Stream (2ch 16bit 48kHz) |
| EP3 | OUT | Interrupt | 64B | HID Output |
| EP4 | IN | Interrupt | 64B | HID Input (1ms poll) |
| EP5 | - | CDC | - | Serial debug (ENABLE_SERIAL only) |
| EP7 | IN | Interrupt | 8B | Wake keyboard (ENABLE_WAKE_HID only) |

## USB Interfaces (DS5 mode)
| # | Class | Purpose |
|---|-------|---------|
| 0 | Audio Control | AC header + terminals + feature units |
| 1 | Audio Streaming OUT | Speaker (4ch/16bit/48kHz) |
| 2 | Audio Streaming IN | Mic (2ch/16bit/48kHz) |
| 3 | HID | Gamepad + Touchpad |
| 4-5 | CDC | Serial debug (optional) |
| N | HID Boot Keyboard | Wake key (optional) |

## Config Descriptor Length
- Base (Audio+HID): 0x00E3 (227 bytes)
- `+TUD_CDC_DESC_LEN` when ENABLE_SERIAL (CDC macro includes its own IAD)
- `+25` when ENABLE_WAKE_HID (interface+HID+EP)

## PS5 Authentication
- Feature reports 0xF0 (SET nonce) / 0xF1 (GET state) / 0xF2 (GET signed)
- Auth data forwarded between USB host and DS5 via BT control channel
- Allows PS5 to verify controller authenticity through DS5's crypto chip
