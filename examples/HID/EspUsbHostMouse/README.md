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
  - `event.rawData`, `event.rawLength` — raw HID input report bytes
  - `event.reportData`, `event.reportLength` — mouse report bytes after removing the Report ID when one is present

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=046d pid=c52b class=0x00(Device) speed=full product="USB Receiver"
left   press
pos: x=3 y=-2  delta: dx=3 dy=-2 wheel=0
pos: x=3 y=-3  delta: dx=0 dy=-1 wheel=0
left   release
pos: x=3 y=-3  delta: dx=0 dy=0 wheel=-1
```

## Walkthrough and checks

### Before running

Decode mouse motion and individual button transitions. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`onMouse()` reports relative motion and button bitmasks. Move, stop, scroll and click separately; software posX/posY are accumulated deltas, not an OS pointer position. Compare buttonMask and changed buttons instead of treating every report as a new click.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostMouse.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
