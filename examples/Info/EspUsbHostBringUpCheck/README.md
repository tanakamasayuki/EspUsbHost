# EspUsbHostBringUpCheck

> 日本語版: [README.ja.md](README.ja.md)

The first sketch to run on a new board, or with a device that will not work. It answers only three questions — did the host controller start, did anything enumerate, and at what speed — and when nothing enumerates it prints the checklist of physical causes instead of an empty log.

Part of the workflow in [docs/usb-host-guide.md](../../../docs/usb-host-guide.md).

## Hardware

- ESP32-S2, ESP32-S3, or ESP32-P4
- Any USB device (a plain USB keyboard or flash drive is the best first test)

## What it does

- Prints the chip, the arduino-esp32 version, the library version, the selected host port, and the endpoint channel budget
- Reports whether `usb.begin()` succeeded, and separates a build/configuration failure from a wiring failure
- Prints a status line every 2 seconds with the device count, channels in use, and free heap
- On connect, prints speed, VID:PID, strings, device class, self/bus powered, `bMaxPower`, and whether the library has a driver for it
- After 10 seconds with no device, prints the VBUS / connector / cable / power / device checklist

## Configuration

`USE_HIGH_SPEED_PORT` (top of the sketch, ESP32-P4 only) selects the high-speed OTG port instead of the full-speed one. Other targets ignore it — ESP32-S2 and ESP32-S3 have a full-speed host only.

## Key APIs

- `usb.begin(config)` / `usb.lastErrorName()`
- `EspUsbHostConfig::port` — `ESP_USB_HOST_PORT_FULL_SPEED` / `ESP_USB_HOST_PORT_HIGH_SPEED` on ESP32-P4
- `usb.deviceCount()`, `usb.endpointChannelCount()`, `usb.maxEndpointChannelCount()`
- `usb.onDeviceConnected()` / `usb.onDeviceDisconnected()`

## Expected Serial output

```
EspUsbHost bring-up check start

--- environment ---
chip           : ESP32-S3 rev 0
arduino-esp32  : 3.3.11
EspUsbHost     : 2.7.8
free heap      : 289012 bytes
host port      : full-speed OTG (this target has no high-speed host)
channel budget : 8 endpoint channels

usb.begin(): ok -- the host controller is running
Plug in a USB device now.
[    2s] devices=0 channels=0/8 heap=270112

ENUMERATED address=1 speed=full-speed (12Mbps)
  045e:07a5 "Microsoft" / "USB Keyboard"
  device class=0x00 subclass=0x00 protocol=0x00 interfaces=2
  library support=yes hub=no max_power=100mA (bus-powered)
  channels claimed=2/8
  Next: run examples/Info/EspUsbHostDeviceExplorer for the full layout.
[    4s] devices=1 channels=2/8 heap=262340
```

## Next step

Once a device enumerates here, run [EspUsbHostDeviceExplorer](../EspUsbHostDeviceExplorer/) to see what its interfaces are and which API drives them.

## Walkthrough and checks

### Before running

Check host startup separately from peripheral enumeration. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Read beginOk and the periodic device count before debugging a class protocol. An empty bus can coexist with successful `usb.begin()`. If no device appears, follow the printed connector/power hints; only after enumeration should you investigate unsupported interfaces.

### API use and adaptation

Treat startup, enumeration, interface discovery and actual class communication as separate checkpoints. A descriptor says what a device advertises; it does not prove a transfer completed or that a protocol is implemented. Keep received lengths and capacity checks when adapting a descriptor walker. An interface number, endpoint address, USB address and physical port identify different things.

First compare the same device directly connected and reconnected. Add a hub or more functions only after the simple case is understood. Preserve the error name and the operation that produced it. Large dumps are diagnostic work, not a pattern for high-rate callbacks.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostBringUpCheck.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
