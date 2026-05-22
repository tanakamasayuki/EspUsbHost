# EspUsbHostCustomDeviceCallbacks

> 日本語版: [README.ja.md](README.ja.md)

Shows how to define your own device connect/disconnect callbacks and add logic around the common `espUsbHostPrint(device)` summary helper.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- Any USB device (keyboard, mouse, hub, etc.)

## What it does

- Prints a common one-line device summary when a USB device connects or disconnects
- Checks the connected device's interfaces and reports whether it looks like a keyboard
- Reports whether the connected device is a USB hub
- Tracks and prints the current device count
- Prints the full descriptor dump when `d` is sent over Serial

## Serial commands

| Command | Action |
|---------|--------|
| `d` | Print full descriptor dump for all connected devices |

## Key APIs

- `usb.onDeviceConnected(onDeviceConnected)` — register a named callback function
- `usb.onDeviceDisconnected(onDeviceDisconnected)` — register a named disconnect callback
- `espUsbHostPrint(device)` — print the common one-line device summary
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — inspect interfaces inside the callback
- `device.isHub` — true when the connected device is a USB hub
- `usb.deviceCount()` — get the number of currently connected devices
- `usb.printAllDeviceInfo()` — optional full descriptor dump

## Expected Serial output

```
EspUsbHost custom device callbacks example start
Press 'd' to print the full descriptor dump.
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
  this device has a keyboard interface
  total devices: 1
disconnected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
  total devices: 0
```
