# EspUsbHostCustomDeviceCallbacks

> 日本語版: [README.ja.md](README.ja.md)

Shows how to define your own device connect/disconnect callbacks and add logic around the common `espUsbHostPrint(device)` summary helper.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- Any USB device (keyboard, mouse, hub, etc.)

## What it does

- Prints a common one-line device summary when a USB device connects or disconnects
- Checks the connected device's interfaces and reports whether it looks like a keyboard
- Reports whether the connected device is a USB hub
- Tracks and prints the current device count
- Prints the full descriptor dump when `d` is sent over Serial

## Serial commands

| Command | Action |
|---------|--------|
| `d` | Print full descriptor dump for all connected devices |

## Key APIs

- `usb.onDeviceConnected(onDeviceConnected)` — register a named callback function
- `usb.onDeviceDisconnected(onDeviceDisconnected)` — register a named disconnect callback
- `espUsbHostPrint(device)` — print the common one-line device summary
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — inspect interfaces inside the callback
- `device.isHub` — true when the connected device is a USB hub
- `usb.deviceCount()` — get the number of currently connected devices
- `usb.printAllDeviceInfo()` — optional full descriptor dump

## Expected Serial output

```
EspUsbHost custom device callbacks example start
Press 'd' to print the full descriptor dump.
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
  this device has a keyboard interface
  total devices: 1
disconnected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
  total devices: 0
```

## Walkthrough and checks

### Before running

Classify connected devices from their interface information. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`isKeyboardLike()` checks interfaces rather than relying only on the device-level class. Connect a keyboard, then a composite device, and send d for a complete dump. Registering a connection callback does not add a new USB class driver.

### API use and adaptation

Treat startup, enumeration, interface discovery and actual class communication as separate checkpoints. A descriptor says what a device advertises; it does not prove a transfer completed or that a protocol is implemented. Keep received lengths and capacity checks when adapting a descriptor walker. An interface number, endpoint address, USB address and physical port identify different things.

First compare the same device directly connected and reconnected. Add a hub or more functions only after the simple case is understood. Preserve the error name and the operation that produced it. Large dumps are diagnostic work, not a pattern for high-rate callbacks.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostCustomDeviceCallbacks.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
