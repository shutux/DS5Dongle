# DS5Dongle — Switch Pro Controller Protocol Reference

## USB Identifiers
| Field | Value |
|-------|-------|
| VID | 0x057E (Nintendo) |
| PID | 0x2009 (Pro Controller) |
| bcdDevice | 0x0210 |

## HID Reports
| ID | Dir | Size | Purpose |
|----|-----|------|---------|
| 0x30 | IN | 63B | Full input (buttons, sticks, IMU) |
| 0x21 | IN | 63B | Subcommand reply |
| 0x81 | IN | 63B | USB command reply |
| 0x01 | OUT | 63B | Subcommand (with rumble) |
| 0x10 | OUT | 63B | Rumble only |
| 0x80 | OUT | 63B | USB command |
| 0x82 | OUT | 63B | USB command 2 |
| 0xF6 | Feature | 63B | DS5Dongle config SET |
| 0xF7 | Feature | 63B | DS5Dongle config GET |

## 0x30 Input Report Layout (63 bytes)
| Offset | Field |
|--------|-------|
| 0 | Timer counter (increment by 3 each report) |
| 1 | Battery level (high nibble) + connection info (low nibble) |
| 2 | Buttons Right: Y(0) X(1) B(2) A(3) SR(4) SL(5) R(6) ZR(7) |
| 3 | Buttons Shared: -(0) +(1) RS(2) LS(3) Home(4) Capture(5) |
| 4 | Buttons Left: Down(0) Up(1) Right(2) Left(3) SR(4) SL(5) L(6) ZL(7) |
| 5-7 | Left Stick (12-bit X, 12-bit Y packed) |
| 8-10 | Right Stick (12-bit X, 12-bit Y packed) |
| 11 | Vibrator input report |
| 12-47 | IMU: 3 samples × 12B (ax,ay,az,gx,gy,gz each int16_le) |

## 12-bit Stick Encoding
```c
data[0] = x & 0xFF;
data[1] = ((y & 0x0F) << 4) | ((x >> 8) & 0x0F);
data[2] = (y >> 4) & 0xFF;
```
- Center value: 2048
- Range: 0-4095
- Conversion from DS5 (0-255): `value * 4095 / 255`

## DS5 → Switch Button Mapping
| DS5 | Switch |
|-----|--------|
| Square | Y |
| Triangle | X |
| Cross | B |
| Circle | A |
| L1 | L |
| R1 | R |
| L2 (or TriggerLeft>32) | ZL |
| R2 (or TriggerRight>32) | ZR |
| Create | Minus (-) |
| Options | Plus (+) |
| PS Home | Home |
| Touchpad | Capture |
| L3 | LS |
| R3 | RS |
| D-pad | D-pad (direct map) |

## IMU Conversion
- Accelerometer: DS5 8192 LSB/g → Switch 4096 LSB/g → `value / 2`
- Gyroscope: DS5 16 LSB/°/s → Switch 14.284 LSB/°/s
- Axis remap: `Switch_X = -DS5_Z, Switch_Y = -DS5_X, Switch_Z = DS5_Y`

## Subcommand Handling (OUT report 0x01)
| Subcmd | Function | Response |
|--------|----------|----------|
| 0x01 | Pairing | ACK |
| 0x02 | Device Info | FW 0x0348, type=0x03 (Pro) |
| 0x03 | Set input mode | 0x30 full / 0x3F simple |
| 0x04 | Trigger buttons elapsed | ACK |
| 0x08 | Shipment | ACK |
| 0x10 | SPI Flash Read | Virtual data |
| 0x21 | NFC MCU config | ACK |
| 0x30 | Player LEDs | ACK |
| 0x38 | HOME Light | ACK |
| 0x40 | Enable IMU | ACK |
| 0x41 | IMU sensitivity | ACK |
| 0x48 | Enable vibration | ACK |

## Virtual SPI Flash Data
| Address | Content |
|---------|---------|
| 0x6000 | Serial number (0xFF×N) |
| 0x601B | Color flag (0x01) |
| 0x6020 | Factory IMU calibration (24B) |
| 0x603D | Stick calibration (25B) |
| 0x6050 | Controller colors (body/buttons/grips) |
| 0x6080 | Factory sensor parameters (24B) |
| 0x6098 | Stick parameters 2 (18B) |
| 0x8010/8026/8028 | User calibration (0xFF = not set) |

## USB 0x80 Commands
| Subcmd | Function | Response |
|--------|----------|----------|
| 0x01 | Status request | Reply 0x81 [0x01, 0x03] (ready) |
| 0x02 | Handshake | Reply 0x81 [0x02] |
| 0x04 | Set HID-only mode | Reply 0x81 [0x04], start 0x30 reports |

## HD Rumble → DS5 Conversion
- Parse left motor (bytes 0-3) and right motor (bytes 4-7) from Switch rumble data
- Left amp → DS5 `heavy` motor (大马达)
- Right amp → DS5 `soft` motor (小马达)
- Send BT output with `valid_flag0 = 0x03` (compat rumble + haptics)

## Report Timing
- `REPORT_INTERVAL_US = 15000` (15ms ≈ 66.7Hz)
- Timer counter increments by 3 per report (simulates 3 IMU samples per interval)
- Reports only sent after Switch completes USB handshake (0x80 commands)

## USB Config Descriptor (Switch mode)
- Single HID Interface, bus powered, remote wakeup, 500mA
- EP1 IN: Interrupt, 64B, 8ms interval
- EP1 OUT: Interrupt, 64B, 8ms interval
- 219-byte HID report descriptor (separate from DS5's 305/421-byte descriptor)
