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
