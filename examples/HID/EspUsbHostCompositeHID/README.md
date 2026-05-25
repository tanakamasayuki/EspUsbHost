# EspUsbHostCompositeHID

> 日本語版: [README.ja.md](README.ja.md)

Reads several common HID functions at the same time, either from one composite USB HID device or from multiple USB HID devices connected through a hub.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- One composite USB HID device or multiple USB HID devices. Examples:
  - keyboard with built-in trackpad
  - keyboard with media keys
  - wireless receiver that exposes keyboard and mouse interfaces
  - separate USB keyboard and USB mouse connected through a hub

## What it does

- Registers one `EspUsbHost` instance and listens for multiple parsed HID callbacks
- Prints keyboard key presses with device address and interface number
- Prints mouse/trackpad movement, button changes, and wheel movement
- Prints Consumer Control usages such as volume and playback keys
- Prints System Control usages such as power, standby, and wake
- Uses `event.address` and `event.interfaceNumber` to identify which device/interface produced each event

This example focuses on input reports. Keyboard LEDs and other output reports are intentionally left out because they often depend on the device's report IDs and output report layout.

## Key APIs

- `usb.onKeyboard(callback)` — fired on keyboard key press/release events
- `usb.onMouse(callback)` — fired on mouse or trackpad events
- `usb.onConsumerControl(callback)` — fired on Consumer Page controls such as media keys
- `usb.onSystemControl(callback)` — fired on System Control usages
- `event.address` / `event.interfaceNumber` — useful for seeing which device and interface produced the event
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## Expected Serial output

```
EspUsbHost multiple HID example start
connected: device: address=1 portId=0x01 vid=05ac pid=0262 class=0x00(Device) speed=full product="Keyboard with Trackpad"
keyboard: address=1 iface=0 keycode=0x04 ascii='a'
mouse: address=1 iface=1 pos: x=3 y=-2 delta: dx=3 dy=-2 wheel=0
consumer: address=1 iface=2 press usage=0x00e9 Volume Up
consumer: address=1 iface=2 release usage=0x00e9 Volume Up
system: address=1 iface=3 press usage=0x82 Standby
```

## See also

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) — basic keyboard text input
- [EspUsbHostMouse](../EspUsbHostMouse/) — basic mouse movement and button handling
- [EspUsbHostConsumerControl](../EspUsbHostConsumerControl/) — media key handling
- [EspUsbHostSystemControl](../EspUsbHostSystemControl/) — power/sleep/wake handling
- [EspUsbHostHIDRawDump](../EspUsbHostHIDRawDump/) — raw HID report dump for troubleshooting
