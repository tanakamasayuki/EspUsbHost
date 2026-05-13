# EspUsbHostDeviceInfo

> 日本語版: [README.ja.md](README.ja.md)

Prints detailed USB device information (descriptor fields, interfaces, and endpoints) for every connected device.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- Any USB device

## What it does

- Prints full descriptor info automatically when a device connects
- Prints disconnect notifications with address, VID, and PID
- Re-prints info for all currently connected devices when `r` is sent over Serial

## Serial commands

| Command | Action |
|---------|--------|
| `r` | Re-print all connected device info |

## Information displayed

**Device level:**
- USB address, parent address/port (for hub-attached devices), bus speed
- VID:PID, device class/subclass/protocol
- USB version, device version, EP0 max packet size
- Manufacturer, product, and serial number strings
- Configuration value, interface count, total config length, attributes, max power

**Interface level:**
- Interface number, alternate setting, class/subclass/protocol, endpoint count

**Endpoint level:**
- Interface number, endpoint address, direction (IN/OUT), transfer type, max packet size, interval

## Key APIs

- `usb.getDevices(devices, maxDevices)` — enumerate all connected devices
- `usb.getDevice(address, device)` — fetch descriptor for a single device
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — fetch interface list
- `usb.getEndpoints(address, endpoints, maxEndpoints)` — fetch endpoint list
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## Expected Serial output

```
EspUsbHostDeviceInfo start
Press 'r' to reprint connected devices.
CONNECTED address=1

=========== USB Device ===========
Address 1 parent=root speed=full-speed
VID:PID 045e:07a5 class=0x00(per-interface) subclass=0x00 protocol=0x00
USB 2.00 device 0.01 ep0=8
Strings manufacturer="Microsoft" product="USB Keyboard" serial=""
Configuration value=1 interfaces=2 total_len=59 attributes=0xa0 max_power=100mA
  Interface 0 alt=0 class=0x03(HID) subclass=0x01 protocol=0x01 endpoints=1
  Interface 1 alt=0 class=0x03(HID) subclass=0x00 protocol=0x00 endpoints=1
    Endpoint iface=0 ep=0x81 dir=IN type=interrupt max_packet=8 interval=10 attrs=0x03
    Endpoint iface=1 ep=0x82 dir=IN type=interrupt max_packet=8 interval=10 attrs=0x03
========= USB Device End =========
```
