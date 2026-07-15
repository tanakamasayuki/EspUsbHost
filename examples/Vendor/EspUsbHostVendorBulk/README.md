# EspUsbHostVendorBulk

> 日本語版: [README.ja.md](README.ja.md)

Demonstrates the generic (non-HID) vendor-specific interface APIs: claiming a `bInterfaceClass == 0xff` interface, bulk IN/OUT transfers, and EP0 vendor control IN/OUT requests.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- A vendor-specific USB device with bulk IN/OUT endpoints — for example an ESP32-S3 running the `tests/peer/usb_vendor` peer sketch (`EspUsbDeviceVendor`)

## What it does

- Claims the vendor-specific interface on connect (`vendorOpen`) and starts bulk IN reception
- Prints every bulk IN payload received through `onVendorData`
- Sends bulk OUT, reads the receive buffer, and issues EP0 vendor control requests on Serial command

The `tests/peer/usb_vendor` peer echoes a bulk OUT `"ping"` back as `"echo:ping"`, returns its name on control IN `bRequest=0x01`, and accepts control OUT `bRequest=0x02`.

## Serial commands

| Command | Action |
|---------|--------|
| `w` | Bulk OUT `"ping"` (peer echoes `"echo:ping"` back on bulk IN) |
| `r` | Non-blocking bulk read from the per-device receive buffer |
| `c` | EP0 vendor control IN, `bRequest=0x01` |
| `o` | EP0 vendor control OUT, `bRequest=0x02` |

## Key APIs

- `usb.vendorOpen(address)` — explicitly claims the vendor-specific interface and starts bulk IN reception
- `usb.onVendorData(callback)` — fired on each bulk IN payload with `EspUsbHostVendorData`; the `data` pointer is valid only during the callback
- `usb.vendorWrite(data, length, address)` — bulk OUT transfer
- `usb.vendorRead(buffer, length, address)` — non-blocking read from a 512-byte per-device receive buffer
- `usb.vendorControlIn(request, value, index, data, length, &actual, address)` — EP0 vendor control IN (`bmRequestType = 0xc0`)
- `usb.vendorControlOut(request, value, index, data, length, address)` — EP0 vendor control OUT (`bmRequestType = 0x40`)

## Expected Serial output

```
EspUsbHost vendor bulk/control example start
connected: device: address=1 portId=0x01 vid=303a pid=4019 class=0x00(Device) speed=full product="EspUsbDevice USB Vendor"
vendorOpen: ok
bulk write: ok
vendor in iface=0 ep=0x81 len=9 data=echo:ping
bulk read: len=0 data=
control in: ok len=17 data=EspUsbDeviceVendor
control out: ok
```
