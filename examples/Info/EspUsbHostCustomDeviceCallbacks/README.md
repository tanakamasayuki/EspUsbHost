# EspUsbHostCustomDeviceCallbacks

> 日本語版: [README.ja.md](README.ja.md)

Shows how to define your own device connect/disconnect callbacks and add logic around the common `espUsbHostPrint(device)` summary helper.

## What it does

- Prints a common one-line device summary when a USB device connects or disconnects
- Checks the connected device's interfaces and reports whether it looks like a keyboard
- Tracks the current device count
- Prints the full descriptor dump when `d` is sent over Serial

## Key APIs

- `usb.onDeviceConnected(onDeviceConnected)` — register a named callback function
- `usb.onDeviceDisconnected(onDeviceDisconnected)` — register a named disconnect callback
- `espUsbHostPrint(device)` — print the common one-line device summary
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — inspect interfaces inside the callback
- `usb.deviceCount()` — get the number of currently connected devices
- `usb.printAllDeviceInfo()` — optional full descriptor dump
