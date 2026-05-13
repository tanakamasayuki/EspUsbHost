# EspUsbHostUSBSerial

Bridges a USB CDC ACM serial device to the ESP32's UART, forwarding data bidirectionally between Serial and the USB serial port.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB CDC ACM device (USB-to-serial adapter, microcontroller dev board, etc.)

## What it does

- Forwards all data received from the USB serial device to `Serial` (UART)
- Forwards all data typed in the Serial monitor to the USB serial device
- Uses `EspUsbHostCdcSerial` — an Arduino `Stream`/`Print`-compatible wrapper

> **Note:** The sketch has a 5-second startup delay (`delay(5000)`) to allow the Serial monitor to connect before printing the startup message.

## Key APIs

- `EspUsbHostCdcSerial CdcSerial(usb)` — creates a CDC serial stream bound to the `EspUsbHost` instance
- `CdcSerial.begin(baud)` — initializes the CDC serial port at the given baud rate
- `CdcSerial.available()` / `CdcSerial.read()` — receive data from the USB device
- `CdcSerial.write(data)` — send data to the USB device
- `usb.onDeviceConnected(callback)` — notified when a device connects

## Expected Serial output

```
EspUsbHost USB serial example start
connected: vid=0403 pid=6001 product=FT232R USB UART
```

After connection, any data sent from the USB serial device appears in the Serial monitor, and any text typed in the Serial monitor is forwarded to the USB device.
