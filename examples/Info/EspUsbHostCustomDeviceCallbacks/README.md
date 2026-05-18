# EspUsbHostCustomDeviceCallbacks

> 日本語版: [README.ja.md](README.ja.md)

Shows how to define your own device connect/disconnect callbacks instead of using the ready-made Serial dump helpers.

## What it does

- Prints a custom one-line summary when a USB device connects
- Checks the connected device's interfaces and reports whether it looks like a keyboard
- Tracks the current device count
- Prints the full descriptor dump when `d` is sent over Serial

## Key APIs

- `usb.onDeviceConnected(onDeviceConnected)` — register a named callback function
- `usb.onDeviceDisconnected(onDeviceDisconnected)` — register a named disconnect callback
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — inspect interfaces inside the callback
- `usb.deviceCount()` — get the number of currently connected devices
- `usb.printAllDeviceInfo()` — optional full descriptor dump
