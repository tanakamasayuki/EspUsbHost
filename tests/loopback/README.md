# Loopback Tests

> 日本語版: [README.ja.md](README.ja.md)

This directory is kept as the location for ESP32-P4 single-board loopback tests
that run USB Host and USB Device on the same chip.

There are currently no runnable loopback tests in this repository. The
Arduino-ESP32 standard USB Device implementation has ESP32-P4 device-side
control limits that make these tests unsuitable as stable Host regression tests.

Specifically, on ESP32-P4 the Arduino-ESP32 standard USB Device implementation
only runs on the HS side. In a single-P4 loopback setup, that means the device
role uses HS and the host role is fixed to FS. That pairing cannot handle an HS
device from the FS host side, so endpoint claim/allocation fails.

This is only a limitation of directly wiring the same chip's HS device side to
its FS host side for loopback testing. It is not a general problem when using
USB Host or the Arduino-ESP32 standard USB Device feature independently.

ESP32-P4 loopback implementation and validation are handled in the sibling
[`EspUsbDevice`](https://github.com/tanakamasayuki/EspUsbDevice) library. EspUsbDevice explicitly
controls device port, speed, descriptors, and endpoint MPS, then pairs with a
released EspUsbHost version for detailed Host/Device testing.

Only add tests back here when they meet one of these conditions:

- Host-side regression tests that are stable with the Arduino Core standard
  Device implementation.
- Minimal Host-side reproductions for issues first found in EspUsbDevice tests
  and worth keeping in this repository.
- Tests whose execution requirements and split from EspUsbDevice are documented
  in this README and `tests/TEST_PLAN*.md`.
