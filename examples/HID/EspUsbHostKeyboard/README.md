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
- The library automatically tracks CapsLock / NumLock / ScrollLock state, updates LEDs, and reflects lock state in `event.ascii`

## Key APIs

- `usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP)`
- `usb.onKeyboard(callback)` — fired on each key press/release with `EspUsbHostKeyboardEvent`
  - `event.ascii` — character reflecting current CapsLock / NumLock state
  - `event.capsLock` / `event.numLock` / `event.scrollLock` — current lock state after this keypress
- `usb.getKeyboardCapsLock()` / `usb.getKeyboardNumLock()` / `usb.getKeyboardScrollLock()` — read lock state at any time
- `usb.setKeyboardLeds(numLock, capsLock, scrollLock)` — override lock state and LEDs manually
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## Expected Serial output

```
connected: vid=045e pid=07a5 product=USB Keyboard
hello world
disconnected
```

## See also

- [EspUsbHostKeyboardDump](../EspUsbHostKeyboardDump/) — dump raw keycode, modifiers, and ASCII for every event
