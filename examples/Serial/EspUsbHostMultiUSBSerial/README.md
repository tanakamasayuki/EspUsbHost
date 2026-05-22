# EspUsbHostMultiUSBSerial

> 日本語版: [README.ja.md](README.ja.md)

Uses two independent USB serial devices at the same time. For clarity, this example identifies devices by VID and assigns FTDI and CP210x devices to separate `EspUsbHostCdcSerial` streams.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB hub or a board with enough host ports
- FTDI USB serial device (VID `0x0403`)
- CP210x USB serial device (VID `0x10c4`)

## What it does

- Assigns connected devices by `vid` to the FTDI or CP210x stream.
- Sends Serial Monitor input to both connected USB serial devices.
- Prints data received from USB serial devices with `FTDI:` or `CP210x:` prefixes.

## Device Identification Fields

| Field | Use |
|-------|-----|
| `address` | Current USB address. Pass it to `setAddress()` to target a connected device. It can change after unplug/replug. |
| `portId` | Physical/topology location. Use it when you need to remember "the device on this port" across reconnects. |
| `vid` / `pid` | Device type/model identification. This example uses `vid` to split FTDI and CP210x. |
| `manufacturer` / `product` | Human-readable labels. Useful for logs, but not guaranteed unique. |
| `serial` | USB serial-number string when provided by the device. It may be empty. |

## Important Limitations

- Working combinations depend on host resources, USB hub behavior, and each device's class driver support.
- In the ESP32-S3 + USB hub manual test environment, `FTDI + CP210x` passes.
- In the same environment, `FTDI + CH34x` failed due to HCD channel exhaustion.
- This example uses separate USB serial devices. It does not split multiple CDC interfaces inside one composite USB device.

## Key APIs

- `EspUsbHostCdcSerial FtdiSerial(usb)` / `Cp210xSerial(usb)` — create independent stream wrappers
- `device.vid` — select FTDI or CP210x
- `device.address` — current USB address passed to `setAddress()`
- `CdcSerial.setAddress(address)` — bind a stream wrapper to one connected device
- `CdcSerial.setAddress(0)` — leave a stream wrapper unassigned when no device is present

## Expected Serial output

```
EspUsbHost multi USB serial example start
Connect one FTDI device and one CP210x device.
Serial Monitor input is sent to both devices.
connected: device: address=1 portId=0x01 vid=0403 pid=6001 class=0x00(Device) speed=full product="FT232R USB UART"
FTDI assigned: portId=0x01 address=1 serial=
connected: device: address=2 portId=0x02 vid=10c4 pid=ea60 class=0x00(Device) speed=full product="CP2102 USB to UART Bridge Controller"
CP210x assigned: portId=0x02 address=2 serial=0001
FTDI: hello from ftdi
CP210x: hello from cp210x
```
