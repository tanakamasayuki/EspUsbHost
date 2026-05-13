# Peer Tests

> 日本語版: [README.ja.md](README.ja.md)

`tests/peer` contains two-board tests. One ESP32-S3 runs an EspUsbHost sketch as
the USB host, and the peer ESP32-S3 runs an Arduino USB device sketch.

Run from `tests`:

```sh
uv run --env-file .env pytest peer/
```

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
- `custom_hid`: pairs with `USB/examples/CustomHIDDevice`.
- `hid_keyboard`: pairs with the Arduino USB keyboard examples.
- `hid_mouse`: pairs with `USB/examples/Mouse/ButtonMouseControl`.
- `hid_keyboard_mouse`: pairs with `USB/examples/KeyboardAndMouseControl`.
- `hid_consumer_control`: pairs with `USB/examples/ConsumerControl`.
- `hid_system_control`: pairs with `USB/examples/SystemControl`.
- `hid_gamepad`: pairs with `USB/examples/Gamepad`.
- `hid_vendor`: pairs with `USB/examples/HIDVendor`.
- `usb_serial`: pairs with `USB/examples/USBSerial`.
- `usb_midi`: pairs with `USB/examples/MIDI`.

Planned coverage:

- HID output reports and feature reports.
