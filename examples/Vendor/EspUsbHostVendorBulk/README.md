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
| `q` | Start the asynchronous read queue: two 8 KB IN transfers in flight |
| `e` | Stop the read queue and print its counters |

## Key APIs

- `usb.vendorOpen(address)` — explicitly claims the vendor-specific interface and starts bulk IN reception
- `usb.onVendorData(callback)` — fired on each bulk IN payload with `EspUsbHostVendorData`; the `data` pointer is valid only during the callback
- `usb.vendorWrite(data, length, address)` — bulk OUT transfer
- `usb.vendorRead(buffer, length, address)` — non-blocking read from a 512-byte per-device receive buffer
- `usb.vendorControlIn(request, value, index, data, length, &actual, address)` — EP0 vendor control IN (`bmRequestType = 0xc0`)
- `usb.vendorControlOut(request, value, index, data, length, address)` — EP0 vendor control OUT (`bmRequestType = 0x40`)
- `usb.vendorReadQueueBegin(depth, bufferBytes, address)` — keeps several bulk IN transfers outstanding instead of one packet at a time, for a device that streams. `usb.vendorReadStats(address)` says whether the endpoint is being kept busy; `usb.vendorReadQueueEnd(address)` stops it. A transfer size alone, without the queue, is `usb.vendorOpen(address, 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS, bytes)`

## Tuning the stream

`q` starts the queue as `vendorReadQueueBegin(2, 8192)` — two transfers in flight, 8 KB each. The two numbers do different jobs:

- **Transfers in flight (`depth`)** covers the turnaround between completions. At `depth` 1 the endpoint has nothing to answer with while the completed transfer is resubmitted, and `vendorReadStats().starved` counts one on every completion. Two is enough for that to stop.
- **Bytes per transfer (`bufferBytes`)** decides how often the turnaround is paid at all. The default of one max-size packet per transfer is the slowest shape this API has.

**The device usually sets the ceiling, not this side.** Once the endpoint is being kept busy, the host cannot read faster than the device supplies, and further tuning of these two numbers changes nothing. `vendorReadStats()` is what separates the two cases: `starved` counts completions that found nothing else in flight, which is this side not asking often enough, while `bytes / completed` is what the device actually put into each transfer, which is the device not supplying more.

On an ESP32-P4 pair at high speed, the queue was worth several times the one-packet-per-transfer default; `depth` 1 starved on every completion while `depth` 2 starved on none; and past a few kilobytes per transfer the result stopped moving. Where it stopped was set by the peer rather than by this side — with the host code untouched, enlarging how much the peer handed to its own `write()` per call raised the whole sweep. So when a stream is slower than expected and `starved` is already 0, the next change belongs on the device, not here.

**Keep `onVendorData()` short.** Each slot is resubmitted from its own completion, and the callback runs before that resubmit, so the slot stays out of flight until the callback returns. Copy into a ring buffer and do the work elsewhere — see [Callback context](../../../docs/usb-host-advanced.md#8-callback-context).

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

## Walkthrough and checks

### Before running

Open a matching interface and exercise vendor bulk and control transfers. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Use a peer implementing the documented w/r/c/o commands, then compare bulk data with control replies. q/e start and stop the queued receive path. Interface number, endpoint address and USB device address are different identifiers; do not send this protocol to an arbitrary vendor-class device.

### API use and adaptation

USB IN/OUT is always named from the device's viewpoint: IN goes to the host, OUT goes to the device. Keep endpoint direction separate from the application roles of request and response. A bulk transfer is a byte stream, not necessarily a whole command; use the actual length and protocol framing. Control transfers also carry direction, request, value, index and length, not just a payload.

A USB transfer accepted or completed is not an application acknowledgment. Check error/status replies and recover protocol state on disconnect. Callback pointers may be reused after return. For asynchronous or direct buffers, obey the acquire/submit/completion ownership rules and do not overwrite in-flight data. Start with a short known request before tuning queues or transfer sizes.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostVendorBulk.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
