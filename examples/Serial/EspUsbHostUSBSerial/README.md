# EspUsbHostUSBSerial

> 日本語版: [README.ja.md](README.ja.md)

Bridges a USB serial device to the ESP32's UART, forwarding data bidirectionally between Serial and the USB serial port.

Supported device types are detected automatically by VID:

| Type | Examples |
|------|---------|
| CDC ACM | Microcontroller dev boards (Arduino, ESP32, etc.), modems |
| FTDI (VCP) | FT232R, FT231X, and other FTDI chips |
| CP210x (VCP) | Silicon Labs CP2102, CP2104, etc. |
| CH34x (VCP) | CH340, CH341, etc. |

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB serial device (any of the supported types above)

## What it does

- Forwards all data received from the USB serial device to `Serial` (UART)
- Forwards all data typed in the Serial monitor to the USB serial device
- Uses `EspUsbHostCdcSerial` — an Arduino `Stream`/`Print`-compatible wrapper

> **Note:** The sketch has a 5-second startup delay (`delay(5000)`) to allow the Serial monitor to connect before printing the startup message.

## Key APIs

- `EspUsbHostCdcSerial CdcSerial(usb)` — creates a serial stream bound to the `EspUsbHost` instance
- `CdcSerial.begin(baud)` — initializes the serial port at the given baud rate
- `CdcSerial.available()` / `CdcSerial.read()` — receive data from the USB device
- `CdcSerial.write(data)` — send data to the USB device
- `usb.onDeviceConnected(callback)` — notified when a device connects

## Expected Serial output

```
EspUsbHost USB serial example start
connected: vid=0403 pid=6001 product=FT232R USB UART
```

After connection, any data sent from the USB serial device appears in the Serial monitor, and any text typed in the Serial monitor is forwarded to the USB device.
