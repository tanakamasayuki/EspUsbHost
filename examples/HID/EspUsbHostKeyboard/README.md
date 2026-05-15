# EspUsbHostKeyboard

> 日本語版: [README.ja.md](README.ja.md)

Reads keyboard input from a USB HID keyboard, prints typed characters to the Serial monitor, and keeps CapsLock / NumLock / ScrollLock LEDs in sync with key presses.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID keyboard

## What it does

- Registers a Japanese keyboard layout (`ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP`)
- Prints device VID, PID, and product string on connect/disconnect
- Prints printable ASCII characters typed on the keyboard; pressing Enter outputs a newline
- Tracks CapsLock / NumLock / ScrollLock state and updates the keyboard LEDs on each toggle
- Re-applies the current LED state when a keyboard connects

## Key APIs

- `usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP)`
- `usb.onKeyboard(callback)` — fired on each key press/release with `EspUsbHostKeyboardEvent`
- `usb.setKeyboardLeds(numLock, capsLock, scrollLock)` — sends LED state to the keyboard
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## Expected Serial output

```
connected: vid=045e pid=07a5 product=USB Keyboard
hello world
disconnected
```

## See also

- [EspUsbHostKeyboardDump](../EspUsbHostKeyboardDump/) — dump raw keycode, modifiers, and ASCII for every event
