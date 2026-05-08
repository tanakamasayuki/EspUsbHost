# Peer Tests

`tests/peer` contains two-board tests. One ESP32-S3 runs an EspUsbHost sketch as
the USB host, and the peer ESP32-S3 runs an Arduino USB device sketch.

Run from `tests`:

```sh
uv run --env-file .env pytest peer/
```

Current coverage:

- `custom_hid`: pairs with `USB/examples/CustomHIDDevice`.
- `hid_keyboard`: pairs with the Arduino USB keyboard examples.
- `hid_mouse`: pairs with `USB/examples/Mouse/ButtonMouseControl`.
- `hid_keyboard_mouse`: pairs with `USB/examples/KeyboardAndMouseControl`.
- `hid_consumer_control`: pairs with `USB/examples/ConsumerControl`.
- `hid_system_control`: pairs with `USB/examples/SystemControl`.
- `hid_gamepad`: pairs with `USB/examples/Gamepad`.
- `hid_vendor`: pairs with `USB/examples/HIDVendor`.
- `usb_serial`: pairs with `USB/examples/USBSerial`.

Planned coverage:

- HID output reports and feature reports.
