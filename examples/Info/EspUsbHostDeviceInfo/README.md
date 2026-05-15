# EspUsbHostDeviceInfo

> 日本語版: [README.ja.md](README.ja.md)

Prints detailed USB device information for every connected device, including unsupported devices kept for inspection and USB hub capability/status information.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- Any USB device
- Optional: USB hub, preferably one with per-port power switching

## What it does

- Prints full descriptor info automatically when a device connects
- Prints disconnect notifications with address, VID, and PID
- Prints a full topology snapshot after enumeration settles
- Re-prints all currently connected devices and hub status when `r` is sent over Serial
- Shows unsupported devices with VID/PID and descriptors instead of silently hiding them
- Shows USB hub descriptor fields and per-port status when hub information is available

## Serial commands

| Command | Action |
|---------|--------|
| `r` | Re-print all connected device info and hub status |

## Information displayed

**Device level:**
- USB address, port ID, parent address (for hub-attached devices), bus speed
- VID:PID, device class/subclass/protocol
- Whether the library supports the device (`supported=yes/no`) and whether it is a hub
- USB version, device version, EP0 max packet size
- Manufacturer, product, and serial number strings
- Configuration value, interface count, total config length, attributes, bus/self-powered flag, remote wakeup flag, max power

**Interface level:**
- Interface number, alternate setting, class/subclass/protocol, endpoint count

**Endpoint level:**
- Interface number, endpoint address, direction (IN/OUT), transfer type, max packet size, interval

**Hub level:**
- Hub descriptor length and raw descriptor bytes
- Number of downstream ports
- `wHubCharacteristics`
- Per-port power switching (PPPS), ganged power switching, or no power switching
- Per-port over-current, ganged over-current, or no over-current reporting
- Compound-device flag
- Power-on-to-power-good delay and hub controller current

**Hub port level:**
- Port status and change bitfields
- Connected, enabled, suspended, over-current, reset, powered, low-speed, high-speed, test, and indicator flags

## Key APIs

- `usb.getDevices(devices, maxDevices)` — enumerate all connected devices
- `usb.getDevice(address, device)` — fetch descriptor for a single device
- `usb.getHubInfo(hubAddress, hub)` — fetch hub descriptor and decoded hub capabilities
- `usb.getHubPortStatus(hubAddress, port, status, change)` — fetch hub port status/change bitfields
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — fetch interface list
- `usb.getEndpoints(address, endpoints, maxEndpoints)` — fetch endpoint list
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## Expected Serial output

```
EspUsbHostDeviceInfo start
Press 'r' to reprint connected devices and hub status.
CONNECTED address=1

=========== USB Device ===========
Address 1 portId=0x01 parent=root speed=full-speed
VID:PID 045e:07a5 class=0x00(per-interface) subclass=0x00 protocol=0x00
Supported=yes hub=no
USB 2.00 device 0.01 ep0=8
Strings manufacturer="Microsoft" product="USB Keyboard" serial=""
Configuration value=1 interfaces=2 total_len=59 attributes=0xa0(bus-powered remote_wakeup=yes) max_power=100mA
  Interface 0 alt=0 class=0x03(HID) subclass=0x01 protocol=0x01 endpoints=1
  Interface 1 alt=0 class=0x03(HID) subclass=0x00 protocol=0x00 endpoints=1
    Endpoint iface=0 ep=0x81 dir=IN type=interrupt max_packet=8 interval=10 attrs=0x03
    Endpoint iface=1 ep=0x82 dir=IN type=interrupt max_packet=8 interval=10 attrs=0x03
========= USB Device End =========

=========== USB Topology ===========
----------- USB Hub -----------
Hub address=1 ports=4 descriptor_len=9 characteristics=0x00a9
Power switching: per-port=yes ganged=no none=no
Over-current: per-port=yes ganged=no none=no
Compound=no power_on_to_good=100ms controller_current=100mA
Raw hub descriptor: 09 29 04 a9 00 32 64 00 ff
  Port 1 status=0x0303 change=0x0000 connected=yes enabled=yes suspended=no over_current=no reset=no powered=yes low_speed=yes high_speed=no test=no indicator=no
--------- USB Hub End ---------
Tracked devices=1
========= USB Topology End =========
```
