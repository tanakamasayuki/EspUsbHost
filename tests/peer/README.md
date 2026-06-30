# Peer Tests

> 日本語版: [README.ja.md](README.ja.md)

`tests/peer` contains two-board tests. One ESP32-S3 runs an EspUsbHost sketch as
the USB host, and the peer ESP32-S3 runs a matching USB Device sketch.

Most tests are kept as the baseline that checks Host interoperability with the
Arduino Core standard Device stack. `usb_vendor` intentionally pairs with the
sibling `EspUsbDevice` library because Arduino Core does not expose the needed
non-HID vendor-specific bulk/control device API.

Run from `tests`:

```sh
uv run --env-file .env pytest peer/
```

## Hardware wiring

The host board and the peer board must be connected via USB.

Connecting them with a standard USB cable also shares the VBUS (5 V) line between the two boards, which can be problematic when both boards are already powered from separate USB connections (e.g., both plugged into a PC). In that case, connecting only the data lines is safer.

On ESP32-S3, the USB D− and D+ signals are on GPIO19 and GPIO20. Wire only these two pins between the two boards (and a shared GND) and leave the VBUS line unconnected:

| Host board | Peer board |
|------------|------------|
| GPIO19 (D−) | GPIO19 (D−) |
| GPIO20 (D+) | GPIO20 (D+) |
| GND | GND |

> **Note:** If you use a USB cable with the VBUS line cut, or a data-only cable, you can connect normally without worrying about power conflicts.

Peer tests use these Arduino CLI profile names:

- `s3_peer_host`: ESP32-S3 USB host board running EspUsbHost
- `s3_peer_device`: ESP32-S3 USB device peer

Set matching serial ports in `.env`:

```sh
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyACM0
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB0
```

Current coverage:

- `hid_logic`: HID helper logic checks that do not require a peer device.
- `custom_hid`: pairs with an Arduino Core standard Custom HID-style device.
- `hid_keyboard`: pairs with an Arduino Core standard USB keyboard device.
- `hid_mouse`: pairs with an Arduino Core standard USB mouse device.
- `hid_keyboard_mouse`: pairs with an Arduino Core standard keyboard + mouse composite device.
- `hid_consumer_control`: pairs with an Arduino Core standard consumer control device.
- `hid_system_control`: pairs with an Arduino Core standard system control device.
- `hid_gamepad`: pairs with an Arduino Core standard gamepad device.
- `hid_vendor`: pairs with an Arduino Core standard vendor HID device.
- `usb_serial`: pairs with an Arduino Core standard USB CDC device.
- `usb_midi`: pairs with an Arduino Core standard USB MIDI device.
- `usb_audio`: pairs with an Arduino Core standard USB Audio device via `USBAudioCard` speaker output.
- `usb_vendor`: pairs with `EspUsbDeviceVendor` from the sibling `EspUsbDevice` library.

Planned coverage:

- Host-side regression tests that can be reproduced with the Arduino Core standard Device stack.
- Minimal Host-side reproductions for issues first found in EspUsbDevice tests.
