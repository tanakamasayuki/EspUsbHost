# EspUsbHostHIDReportDescriptor

> 日本語版: [README.ja.md](README.ja.md)

Prints HID report descriptors for connected HID devices.

Most applications do not need to show report descriptors during normal use. They are mainly useful when investigating a new HID device, debugging report IDs, or designing support for devices such as gamepads whose axis and button layout varies by product.

## Hardware

- ESP32-S3, or another board supported by Arduino-ESP32 USB Host
- USB HID device such as a keyboard, mouse, gamepad, or custom HID device

## What It Does

- Prints normal USB device information on connect
- Fetches HID report descriptors asynchronously with `onHIDReportDescriptor()`
- Prints the raw descriptor bytes
- Prints a simple decoded list of HID items

## Key APIs

- `usb.onHIDReportDescriptor(callback)` — receive each HID report descriptor after enumeration
- `espUsbHostPrint(descriptor)` — print metadata, raw bytes, and a simple item decode
- `espUsbHostPrintHIDReportDescriptor(data, length)` — print only a raw descriptor buffer

## Expected Output

```
EspUsbHostHIDReportDescriptor start
Connect a HID device to print its HID report descriptor.
CONNECTED address=1 vid=303a pid=4004 product="HID Device"

===== HID Report Descriptor #1 =====
hid report descriptor: address=1 iface=0 hid=0x0111 country=0x00 type=0x22 reported_len=38 len=38
Raw HID report descriptor: 05 01 09 04 A1 01 ...
HID report descriptor: len=38
  0000: 0x05 Global   Usage Page         value=1 raw=01
  0002: 0x09 Local    Usage              value=4 raw=04
  0004: 0xa1 Main     Collection         value=1 raw=01
=== HID Report Descriptor End ===
```

## Walkthrough and checks

### Before running

Capture the HID report descriptor used to interpret input bytes. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Register `onHIDReportDescriptor()` before `begin()`, then reconnect the HID device so its descriptor is observed. Match report ID, field width and usage with a raw-input dump; a configuration descriptor and a HID report descriptor describe different layers.

### API use and adaptation

Treat startup, enumeration, interface discovery and actual class communication as separate checkpoints. A descriptor says what a device advertises; it does not prove a transfer completed or that a protocol is implemented. Keep received lengths and capacity checks when adapting a descriptor walker. An interface number, endpoint address, USB address and physical port identify different things.

First compare the same device directly connected and reconnected. Add a hub or more functions only after the simple case is understood. Preserve the error name and the operation that produced it. Large dumps are diagnostic work, not a pattern for high-rate callbacks.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostHIDReportDescriptor.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
