# EspUsbHostKeyboard

Reads keyboard input from a USB HID keyboard and prints typed characters to the Serial monitor.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID keyboard

## What it does

- Registers a Japanese keyboard layout (`ESP_USB_HOST_KEYBOARD_LAYOUT_JP`)
- Prints device VID, PID, and product string on connect/disconnect
- Prints printable ASCII characters typed on the keyboard; pressing Enter outputs a newline

## Key APIs

- `usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JP)`
- `usb.onKeyboard(callback)` — fired on each key press/release with `EspUsbHostKeyboardEvent`
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## Expected Serial output

```
connected: vid=045e pid=07a5 product=USB Keyboard
hello world
disconnected
```

## See also

- [EspUsbHostKeyboardLeds](../EspUsbHostKeyboardLeds/) — control NumLock / CapsLock / ScrollLock LEDs
