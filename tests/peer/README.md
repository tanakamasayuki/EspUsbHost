# Peer Tests

`tests/peer` contains two-board tests. One ESP32-S3 runs an EspUsbHost sketch as
the USB host, and the peer ESP32-S3 runs an Arduino USB device sketch.

Run from `tests`:

```sh
uv run --env-file .env pytest peer/
```

Current coverage:

- `hid_keyboard`: pairs with the Arduino USB keyboard examples.
- `hid_mouse`: pairs with `USB/examples/Mouse/ButtonMouseControl`.
- `hid_keyboard_mouse`: pairs with `USB/examples/KeyboardAndMouseControl`.

Planned coverage:

- Consumer Control: raw HID input validation for media keys.
- Gamepad: raw HID input validation for buttons, axes, and hat.
- Custom HID / HID Vendor: descriptor and report transport validation.

