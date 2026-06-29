# Test Plan

> 日本語版: [TEST_PLAN.ja.md](TEST_PLAN.ja.md)

## Testing strategy

Tests are divided into two categories based on whether the test environment can be fully controlled by software.

**Automated tests** run in CI or locally without human interaction.
All inputs are generated programmatically; all expected outputs are verified by assertion.

**Manual tests** are used when the environment cannot be fully controlled by software — not because automation is inconvenient, but because the test inherently requires physical hardware that cannot be emulated, or verification that requires human judgment (visual inspection, tactile feedback, etc.).

### Test Split With EspUsbDevice

EspUsbHost `peer/` tests should generally keep their current setup based on the
Arduino-ESP32 standard USB Device implementation. This keeps the Host library
checked against Arduino Core's device stack and avoids overfitting Host behavior
to the sibling `EspUsbDevice` library.

EspUsbDevice tests pair with the released EspUsbHost version for detailed
Host/Device coverage. That repository owns tests for behavior that Arduino
Core's standard USB Device implementation cannot control well, such as
descriptors, report IDs, output/feature reports, composite HID, CDC, MIDI, and
MSC behavior. ESP32-P4 single-board loopback tests are also currently organized
primarily in EspUsbDevice.

ESP32-P4 loopback is not maintained in this repository because Arduino-ESP32's
standard USB Device implementation only runs on the HS side on P4. In a
single-board direct loopback setup, the device role uses HS and the host role is
fixed to FS. The FS host side cannot handle an HS device, so endpoint
claim/allocation fails. This is only a direct loopback limitation; it is not a
general issue when using USB Host or the standard USB Device feature
independently.

Local EspUsbHost checkouts may still be substituted in EspUsbDevice when an
unreleased Host-side fix needs pre-release validation, but the normal acceptance
baseline here remains released EspUsbHost plus the Arduino Core standard Device
implementation.

```
tests/
  peer/       Automated — two ESP32-S3 boards, one host + one device
  loopback/   Reserved — single ESP32-P4 setup; no runnable tests in this repo now
  manual/     Manual — special hardware or human interaction required
```

See each subdirectory's README for hardware setup and individual test details.

---

## Test coverage matrix

| Feature | Automated | Manual | Not covered |
|---------|-----------|--------|-------------|
| HID keyboard input | ✅ peer | | |
| HID keyboard layout (JP) | | | ⬜ |
| HID mouse input | ✅ peer | | |
| HID consumer control | ✅ peer | | |
| HID system control | ✅ peer | | |
| HID gamepad | ✅ peer | | |
| HID vendor input/output | ✅ peer | | |
| HID raw input dump | ✅ peer (custom_hid) | | |
| Keyboard LED output | ✅ peer (hid_logic) | ✅ manual (visual) | |
| USB serial — CDC ACM | ✅ peer, line coding config | | |
| USB serial — VCP (FTDI/CP210x/CH34x) | | ✅ manual, serial format configs | |
| USB MIDI | ✅ peer | | |
| USB audio input/output | ✅ peer (output), partial input | | |
| USB Mass Storage — block I/O / FatFs mount | ✅ peer (capacity, Inquiry/Sense, read/write, out-of-range rejection, write failure reporting) | ✅ manual (real USB flash capacity, LBA0 read, FatFs/VFS mount, `fs::FS` wrapper, file write/read/delete, mounted disconnect/remount) | ⬜ full BOT recovery after failed data phase, multiple LUNs, >32-bit-sector FatFs mount |
| Multiple devices | | ✅ manual | |
| Device hot-plug | | ✅ manual | |
| HUB detection, topology, and port power control | | ✅ manual (`hub_info`, `hub_power`) | ⬜ change bit clear, cascaded hubs, USB 3.x hub compatibility |
