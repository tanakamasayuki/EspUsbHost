# EspUsbHostKeyboardLeds

> 日本語版: [README.ja.md](README.ja.md)

Controls the NumLock, CapsLock, and ScrollLock LEDs on a connected USB keyboard via HID output reports.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID keyboard with indicator LEDs

## What it does

Reads single-character commands from the Serial monitor and toggles the corresponding keyboard LED:

| Command | Action |
|---------|--------|
| `n` | Toggle NumLock |
| `c` | Toggle CapsLock |
| `s` | Toggle ScrollLock |
| `0` | Turn all LEDs off |

## Key APIs

- `usb.setKeyboardLeds(numLock, capsLock, scrollLock)` — sends an HID output report to update LED state
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## Expected Serial output

```
Keyboard LED control
n: NumLock, c: CapsLock, s: ScrollLock, 0: all off
connected: vid=045e pid=07a5 product=USB Keyboard
leds: num=1 caps=0 scroll=0 OK
leds: num=1 caps=1 scroll=0 OK
```

## See also

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) — keyboard input (reading keystrokes)
