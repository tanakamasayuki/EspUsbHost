# EspUsbHostMouse

> 日本語版: [README.ja.md](README.ja.md)

Reads movement and button events from a USB HID mouse, tracks accumulated position, and detects individual button clicks.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID mouse

## What it does

- Accumulates `posX` / `posY` from each movement delta so absolute position is always available
- Detects individual button press and release events (rising/falling edge of each button bit)
- Prints accumulated position and delta on every move event

## Key APIs

- `usb.onMouse(callback)` — fired on each mouse event with `EspUsbHostMouseEvent`
  - `event.buttonsChanged` — true when button state changed
  - `event.moved` — true when position changed
  - `event.x`, `event.y`, `event.wheel` — relative movement deltas (`int16_t`)
  - `event.buttons`, `event.previousButtons` — button bitmask (`uint8_t`)

## Expected Serial output

```
connected: vid=046d pid=c52b product=USB Receiver
left   press
pos: x=3 y=-2  delta: dx=3 dy=-2 wheel=0
pos: x=3 y=-3  delta: dx=0 dy=-1 wheel=0
left   release
pos: x=3 y=-3  delta: dx=0 dy=0 wheel=-1
```
