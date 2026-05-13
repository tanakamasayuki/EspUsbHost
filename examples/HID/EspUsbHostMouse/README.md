# EspUsbHostMouse

> 日本語版: [README.ja.md](README.ja.md)

Reads movement and button events from a USB HID mouse and prints them to the Serial monitor.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID mouse

## What it does

- Prints button state changes when any button is pressed or released
- Prints movement data (X, Y, wheel) whenever the mouse moves

## Key APIs

- `usb.onMouse(callback)` — fired on each mouse event with `EspUsbHostMouseEvent`
  - `event.buttonsChanged` — true when button state changed
  - `event.moved` — true when position changed
  - `event.x`, `event.y`, `event.wheel` — relative movement deltas
  - `event.buttons`, `event.previousButtons` — button bitmask

## Expected Serial output

```
buttons: 0x00 -> 0x01
move: x=3 y=-2 wheel=0 buttons=0x01
move: x=0 y=0 wheel=-1 buttons=0x00
buttons: 0x01 -> 0x00
```
