# Test Plan

> 日本語版: [TEST_PLAN.ja.md](TEST_PLAN.ja.md)

## Testing strategy

Tests are divided into two categories based on whether the test environment can be fully controlled by software.

**Automated tests** run in CI or locally without human interaction.
All inputs are generated programmatically; all expected outputs are verified by assertion.

**Manual tests** are used when the environment cannot be fully controlled by software — not because automation is inconvenient, but because the test inherently requires physical hardware that cannot be emulated, or verification that requires human judgment (visual inspection, tactile feedback, etc.).

```
tests/
  peer/       Automated — two ESP32-S3 boards, one host + one device
  loopback/   Automated — single ESP32-P4 running both host and device (WIP)
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
| USB serial — CDC ACM | ✅ peer | | |
| USB serial — VCP (FTDI/CP210x/CH34x) | | ✅ manual | |
| USB MIDI | ✅ peer | | |
| USB audio input/output | ✅ peer (output), partial input | | |
| Multiple devices | | ✅ manual | |
| Device hot-plug | | ✅ manual | |
| HUB detection | | | ⬜ (not yet implemented) |
