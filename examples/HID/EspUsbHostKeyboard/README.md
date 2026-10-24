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
  - `event.rawData`, `event.rawLength` — raw HID input report bytes
  - `event.reportData`, `event.reportLength` — keyboard report bytes after removing the Report ID when one is present
- `usb.getKeyboardCapsLock()` / `usb.getKeyboardNumLock()` / `usb.getKeyboardScrollLock()` — read lock state at any time
- `usb.setKeyboardLeds(numLock, capsLock, scrollLock)` — override lock state and LEDs manually
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
hello world
disconnected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
```

## See also

- [EspUsbHostKeyboardDump](../EspUsbHostKeyboardDump/) — dump raw keycode, modifiers, and ASCII for every event

## Walkthrough and checks

### Before running

Read a USB keyboard and follow its Lock LED state. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Register `onKeyboard()` before `usb.begin()`. The callback ignores releases and prints printable event.ascii values. Type letters, Enter and Caps Lock, then compare the character and LED changes; a missing character does not mean a missing HID report.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostKeyboard.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
