# EspUsbHostProtocolConsole

> 日本語版: [README.ja.md](README.ja.md)

An interactive console for working out an undocumented USB protocol. Type USB transfers on the serial monitor and see what the device answers, with no rebuild between attempts.

The intended workflow: capture the device on a PC with USBPcap + Wireshark (or `usbmon` on Linux), pick one transfer out of the capture, and replay it here byte for byte. When the answer matches the capture, that part of the protocol is understood and can be written into a sketch. Part of the workflow in [docs/usb-host-guide.md](../../../docs/usb-host-guide.md).

## Hardware

- ESP32-S2, ESP32-S3, or ESP32-P4
- The USB device whose protocol you are working out

## Commands

One command per line. All numbers are hex, with or without a `0x` prefix.

| Command | Description |
|---------|-------------|
| `help` | Print the command list |
| `list` | Devices, interfaces and endpoints, with the current target marked |
| `addr <a>` | Select the target device address (default: the first device) |
| `open <iface> [ondemand]` | Claim an interface for bulk transfers. `ondemand` leaves the bulk IN endpoint idle until `in` asks for data, which is what a request/response protocol needs |
| `ctl <bmRequestType> <bRequest> <wValue> <wIndex> <len\|bytes...>` | One EP0 control transfer. IN when bit 7 of `bmRequestType` is set — give the length. OUT — give the data bytes, or nothing |
| `out <bytes...>` | Bulk OUT |
| `in [len] [timeout_ms]` | One bulk IN, waited for (default 64 bytes, 1000 ms) |
| `zlp` | Zero-length bulk OUT packet, for protocols that need one to terminate a transfer |
| `mon on\|off` | Print bulk IN data that arrives unasked (continuous mode) |
| `desc` | Raw configuration descriptor of the target device |

## Session example

```
> list
device address=1 0483:070b "Xprinter" "Printer"  <= target
  interface 0 class=0x07/0x01/0x02 claimed=no
    ep 0x01 OUT attrs=0x02 max_packet=64 interval=0
    ep 0x82 IN  attrs=0x02 max_packet=64 interval=0

> ctl 80 06 0100 0000 12
ctl type=0x80 req=0x06 value=0x0100 index=0x0000 len=18: ok (2ms)
  0000  12 01 00 02 00 00 00 40 83 04 0b 07 00 01 01 02  |.......@........|
  0010  00 01                                            |..|

> open 0
open iface=0 mode=continuous: ok
  bulk out ep=0x01 mps=64 / bulk in ep=0x82 mps=64

> out 10 04 01
out len=3: ok
in  address=1 iface=0 ep=0x82 len=1
  0000  16                                               |.|
```

`ctl 80 06 0100 0000 12` is a standard `GET_DESCRIPTOR(DEVICE)`: `bmRequestType=0x80` (IN, standard, device), `bRequest=0x06`, `wValue=0x0100` (descriptor type 1, index 0), 18 bytes. It is the safest first command to confirm the console reaches the device.

## Reading failures

A failure is information, not just an error:

- **`ctl` fails** — the device stalled the request, which means it does not support it. Wrong `bmRequestType` recipient (device vs interface) and a wrong `wIndex` are the usual causes; a class request usually addresses an interface, so `wIndex` is the interface number.
- **`open` fails** — the library already claimed that interface for a class driver of its own, or the interface number does not exist. `list` shows `claimed=yes` for interfaces the library owns.
- **`in` times out** — many devices answer only inside a transaction. Open with `ondemand`, send the request with `out` first, then read.
- **`out` fails** — no interface is open, or the endpoint halted after an earlier error. Reconnect the device to clear it.

## Limits

- One control transfer carries at most 248 data bytes (256 minus the 8-byte setup packet), which is the ESP-IDF host stack's limit.
- `out` and `in` are capped at 512 bytes per command by the sketch's buffers.
- This console drives bulk and control transfers. Isochronous endpoints (audio, video) are not reachable this way.

## Key APIs

- `usb.vendorOpen(address, interfaceNumber, readMode)` — claim any interface, whatever its class
- `usb.vendorControlTransfer(...)` — one EP0 transfer with a caller-supplied `bmRequestType`
- `usb.vendorWrite()` / `usb.vendorReadSync()` / `usb.vendorWriteZlp()`
- `usb.onVendorData()` — bulk IN payloads in continuous mode
