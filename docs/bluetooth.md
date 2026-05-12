# DS5Dongle — Bluetooth Reference

## Connection Flow
```
bt_init() → l2cap_init() + SSP config + HCI power on
  ↓
HCI_STATE_WORKING → gap_inquiry_start(30s)
  ↓
Inquiry Result (CoD=peripheral 0x0500) → gap_inquiry_stop()
  ↓
hci_create_connection → ACL link
  ↓
HCI_EVENT_CONNECTION_COMPLETE → hci_authentication_requested
  ↓
Link Key Request → reply stored / negative reply (forces re-pair)
  ↓
User Confirmation (SSP Just Works) → auto accept
  ↓
Authentication Complete → hci_set_connection_encryption(handle, 1)
  ↓
Encryption Change (enabled) → open L2CAP HID channels
  ↓
L2CAP_EVENT_CHANNEL_OPENED (control+interrupt) → SET_PROTOCOL (boot→report)
  ↓
Connected — data flows on interrupt channel
```

## SSP Configuration
- `gap_ssp_set_enable(true)` + `gap_secure_connections_enable(true)`
- IO: `SSP_IO_CAPABILITY_DISPLAY_YES_NO`
- Auth: `SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING`
- Legacy PIN: "0000"

## L2CAP PSMs
| PSM | Purpose | MTU |
|-----|---------|-----|
| 0x0011 | HID Control (Feature Reports) | 256 |
| 0x0013 | HID Interrupt (Input/Output) | 1691 |

## Reconnection
- L2CAP services registered for both PSMs → DS5 can reconnect on its own
- `HCI_EVENT_CONNECTION_REQUEST` accepts inbound from gamepad CoD
- When `new_pair=false`, don't initiate L2CAP channels — wait for DS5

## Inquiry Restart (CRITICAL)
- Both `GAP_EVENT_INQUIRY_COMPLETE` and `HCI_EVENT_INQUIRY_COMPLETE` must restart inquiry if no device found
- Upstream bug `3a179c7` broke this by splitting the fall-through — fixed in `30e11f0`
- Only `HCI_EVENT_INQUIRY_COMPLETE` triggers `gap_inquiry_start()` (per upstream fix)

## Data Send/Receive
- **Receive**: `L2CAP_DATA_PACKET` dispatched by CID (interrupt vs control)
- **Send**: `bt_write()` → `0xA2` + payload + CRC32 → `send_fifo` (10 slots) → `L2CAP_EVENT_CAN_SEND_NOW`
- Feature GET: send `0x43 <report_id>` to control channel
- Feature SET: send `0x53 <report_id> <data...>` + CRC32 to control channel

## CRC32 Seeds
| Context | Seed | Header byte |
|---------|------|-------------|
| Output Report | `~0xEADA2D49` | 0xA2 |
| Feature Report | `~0x2060efc3` | 0x53 |
- Polynomial: 0xEDB88320 (CRC32-C variant)

## BT Report Formats
- **BT Input** (DS5→Pico): `0xA1 0x31 <77B payload>` — `data[3:]` = USBGetStateData (63B)
- **BT Output** (Pico→DS5): `0xA2 0x31 <seq_hi4> <tag=0x10> <SetStateData[74]>` + CRC32
- **BT Audio** (0x36, 398B): StateData + Haptics(64B int8 3kHz) + Opus(200B 48kHz)

## Link Key Storage
- BTstack built-in: `gap_get_link_key_for_bd_addr()` / `gap_drop_link_key_for_bd_addr()`
- Auth failure → drop key → force re-pair
- Stored in flash via btstack TLV

## Disconnect Handling
- `bt_disconnect()` → `l2cap_disconnect(interrupt)` + `l2cap_disconnect(control)` + `gap_disconnect(handle)`
- On `HCI_EVENT_DISCONNECTION_COMPLETE`: restart inquiry
- Send queue drained on disconnect (upstream PR#35)
