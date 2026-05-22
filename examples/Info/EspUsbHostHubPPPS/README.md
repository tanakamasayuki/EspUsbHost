# EspUsbHostHubPPPS

> 日本語版: [README.ja.md](README.ja.md)

Controls power on a USB hub port that supports per-port power switching (PPPS).

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB hub with per-port power switching support
- Any USB device connected to one downstream hub port

For this example, a USB 2.0 hub is recommended. A self-powered hub is preferable for stable port power behavior, but models that are easy to buy consistently and also work well with PPPS are limited.

## What it does

- Prints hub power-switching capability and port status
- Lets you select a downstream port from Serial
- Turns the selected port power off or on with `usb.setHubPortPower()`
- Prints connect/disconnect callbacks so the effect is visible

## Serial commands

| Command | Action |
|---------|--------|
| `r` | Refresh hub information and all port status |
| `1`-`9` | Select a hub port |
| `o` | Power off the selected port |
| `n` | Power on the selected port |

## Notes

- The hub must report PPPS support for true per-port control.
- USB 3.x hubs may expose different internal topology, so start with a USB 2.0 hub when checking this feature.
- Some hubs use ganged power switching. On those hubs, a port power command may affect all ports or fail.
- Turning a port off disconnects the device on that port.

## Key APIs

- `usb.getHubInfo(hubAddress, hub)` — fetch hub descriptor and decoded hub capabilities
- `usb.getHubPortStatus(hubAddress, port, status, change)` — fetch hub port status/change bitfields
- `usb.setHubPortPower(hubAddress, port, enable)` — turn hub port power on or off

## Expected Serial output

```
EspUsbHostHubPPPS start
Connect a USB hub that supports per-port power switching.
Commands:
  r  refresh hub and port status
  1-9  select port
  o  power off selected port
  n  power on selected port
CONNECTED address=1 portId=0x01 hub=1 vid=05e3 pid=0608
hub address=1 ports=4 ppps=1 ganged_power=0 no_power_switch=0
  port 1: status=0x0100 change=0x0000 powered=1 connected=0 enabled=0
  port 2: status=0x0303 change=0x0000 powered=1 connected=1 enabled=1
  port 3: status=0x0100 change=0x0000 powered=1 connected=0 enabled=0
  port 4: status=0x0100 change=0x0000 powered=1 connected=0 enabled=0
```

After selecting port 2 with `2` and powering it off with `o`:

```
selected port=2
  port 2: status=0x0303 change=0x0000 powered=1 connected=1 enabled=1
hub=1 port=2 power=off
request: OK
  port 2: status=0x0100 change=0x0001 powered=1 connected=0 enabled=0
DISCONNECTED address=2 portId=0x02 vid=045e pid=07a5
```
