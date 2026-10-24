# EspUsbHostHIDRawDump

> 日本語版: [README.ja.md](README.ja.md)

Dumps raw HID input reports from all connected devices with device address, interface, and protocol information.
Supports multiple simultaneous USB devices.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- One or more USB HID devices (keyboard, mouse, gamepad, etc.)

## What it does

- Prints device address, VID, PID, and product name on connect/disconnect
- Dumps raw HID input reports as space-separated hex with address and interface metadata

## Key APIs

- `usb.onHIDInput(callback)` — fired on every HID input report with `EspUsbHostHIDInput`
  - `input.address` — USB device address (distinguishes multiple devices)
  - `input.vid`, `input.pid` — USB vendor/product IDs for device-specific handling
  - `input.product` — USB product string when available
  - `input.interfaceNumber` — HID interface number
  - `input.subclass` — HID subclass (0 = no subclass, 1 = boot interface)
  - `input.protocol` — HID protocol (0 = none, 1 = keyboard, 2 = mouse)
  - `input.length` — report byte length
  - `input.data` — raw report bytes
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`
  - `device.address` — USB device address
- `espUsbHostPrint(input)` / `espUsbHostPrint(device)` — one-line Serial dump helpers used by this example

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
hid: address=1 iface=0 subclass=0x01 protocol=0x01 len=8 data=00 00 00 00 00 00 00 00
hid: address=1 iface=0 subclass=0x01 protocol=0x01 len=8 data=00 00 04 00 00 00 00 00
disconnected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
```

## See also

- [EspUsbHostDeviceInfo](../../Info/EspUsbHostDeviceInfo/) — full device/interface/endpoint enumeration

## Walkthrough and checks

### Before running

Print raw HID input before applying an application-specific interpretation. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`onHIDInput()` is useful when no specialized callback describes your device. Change one input at a time and compare byte positions, report IDs and lengths. Use the descriptor example next; changing bytes alone does not establish their signedness or units.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostHIDRawDump.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
