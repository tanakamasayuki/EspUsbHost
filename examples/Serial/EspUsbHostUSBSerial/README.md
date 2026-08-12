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
- `CdcSerial.setRxBufferSize(bytes)` — resizes the receive ring (commented out in the sketch; see below)
- `CdcSerial.available()` / `CdcSerial.read()` — receive data from the USB device
- `CdcSerial.write(data)` — send data to the USB device
- `usb.onDeviceConnected(callback)` — notified when a device connects

## Receive buffer size

Incoming bytes are stored in a ring buffer that the USB client task fills and `read()` drains. It holds 512 bytes by default, and when it overflows the oldest byte is dropped without any error — the symptom is missing or garbled data, not a failure return.

512 bytes is about 5.5 ms of traffic at 921600 baud and about 44 ms at 115200, so it runs out whenever `loop()` cannot drain it in time (a blocking WiFi call, an SD write, a display refresh) or when the device sends in bursts (a GPS emitting a second of NMEA at once, a boot-time log dump). If you see loss, enlarge the ring — the sketch has the call ready, commented out:

```cpp
// CdcSerial.setRxBufferSize(8192);
CdcSerial.begin(115200);
```

It allocates from the heap, so it must be called before `begin()` (or after `end()`) — the USB client task writes into the ring while the port is attached. It returns `false` if the port is attached, if the size is below 2, or if the allocation fails. `CdcSerial.rxBufferSize()` reports the current size.

Changing the compile-time default instead is possible but not recommended; see the "USB serial (CDC ACM and VCP)" section of the [main README](../../../README.md).

## Expected Serial output

```
EspUsbHost USB serial example start
connected: device: address=1 portId=0x01 vid=0403 pid=6001 class=0x00(Device) speed=full product="FT232R USB UART"
```

After connection, any data sent from the USB serial device appears in the Serial monitor, and any text typed in the Serial monitor is forwarded to the USB device.
