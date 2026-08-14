# EspUsbHostDeviceExplorer

> 日本語版: [README.ja.md](README.ja.md)

Identify an unknown USB device and decide how to drive it. For every connected device it prints the parsed descriptor dump, one line per interface saying what that class is and which EspUsbHost API or example covers it, and the raw descriptor bytes — including the class-specific descriptor blocks (HID, CDC functional, CCID, UAC) that the parsed dump does not show.

The raw section is what you compare against a USBPcap/Wireshark capture or `lsusb -v` output taken on a PC. Part of the workflow in [docs/usb-host-guide.md](../../../docs/usb-host-guide.md).

## Hardware

- ESP32-S2, ESP32-S3, or ESP32-P4
- The USB device to investigate

## What it does

- Waits for enumeration to settle (a composite device raises several events), then dumps automatically
- `printAllDeviceInfo()` — the parsed device, interface, endpoint and channel state
- Per interface: the class name in words, the API or example that drives it, and its endpoints with direction, transfer type, max packet size and interval
- Raw `GET_DESCRIPTOR(DEVICE)` and `GET_DESCRIPTOR(CONFIGURATION)` bytes as a hex/ASCII dump
- A walk of the configuration descriptor block by block: offset, `bLength`, `bDescriptorType` with its name, and the raw bytes of each block

## Serial commands

| Key | Action |
|-----|--------|
| `d` | Dump everything again |
| `r` | Raw descriptors only |

## Control transfer limit

ESP-IDF's precompiled host stack caps one control transfer at 256 bytes including the 8-byte setup packet, so at most 248 descriptor bytes are readable in one request. When `wTotalLength` is larger, the sketch says so and dumps the first 248 bytes. This is the same limit that stops USB cameras (UVC) from enumerating at all — a device whose configuration descriptor exceeds 256 bytes fails during enumeration, before any class driver runs.

## Key APIs

- `usb.printAllDeviceInfo()` — parsed dump for every device
- `usb.getDevices()` / `usb.getInterfaces()` / `usb.getEndpoints()` — the descriptor state as data
- `usb.vendorControlTransfer(0x80, 0x06, ...)` — a standard `GET_DESCRIPTOR` on EP0. EP0 belongs to the device, not to an interface, so this works without `vendorOpen()`
- `usb.onDeviceConnected()` / `usb.onDeviceDisconnected()`

## Expected Serial output

```
connected address=1 045e:07a5 "USB Keyboard"

=========== USB Device ===========
...printAllDeviceInfo() output...
========= USB Device End =========

--- how to drive address=1 (045e:07a5) ---
interface 0 alt=0 class=0x03/0x01/0x01 claimed=yes
  what : HID boot keyboard
  how  : library API: onKeyboard() -- examples/HID/EspUsbHostKeyboard
  ep   : 0x81 IN  interrupt   max_packet=8 interval=10
interface 1 alt=0 class=0x03/0x00/0x00 claimed=yes
  what : HID (report protocol)
  how  : library API: onHIDInput()/onHIDVendorInput() -- examples/HID/EspUsbHostHIDRawDump
  ep   : 0x82 IN  interrupt   max_packet=8 interval=10

--- raw DEVICE descriptor (address=1) ---
  0000  12 01 00 02 00 00 00 08 5e 04 a5 07 01 01 01 02  |........^.......|
  0010  00 01                                            |..|
--- raw CONFIGURATION descriptor (address=1) ---
  wTotalLength=59 read=59 bytes
  0000  09 02 3b 00 02 01 00 a0 32 09 04 00 00 01 03 01  |..;.....2.......|
  ...
--- configuration descriptor blocks ---
  offset=  0 len= 9 type=0x02 CONFIGURATION                            09 02 3b 00 02 01 00 a0 32
  offset=  9 len= 9 type=0x04 INTERFACE                                09 04 00 00 01 03 01 01 00
  offset= 18 len= 9 type=0x21 HID / CDC-or-class-specific (0x21)       09 21 11 01 00 01 22 41 00
  offset= 27 len= 7 type=0x05 ENDPOINT                                 07 05 81 03 08 00 0a
  ...

=== end of dump ('d' dump again, 'r' raw descriptors only) ===
```

## Next steps

- HID device: [EspUsbHostHIDReportDescriptor](../EspUsbHostHIDReportDescriptor/) for the report layout, then [EspUsbHostHIDRawDump](../../HID/EspUsbHostHIDRawDump/)
- Unknown bulk protocol: [EspUsbHostProtocolConsole](../../Vendor/EspUsbHostProtocolConsole/) to try transfers by hand
