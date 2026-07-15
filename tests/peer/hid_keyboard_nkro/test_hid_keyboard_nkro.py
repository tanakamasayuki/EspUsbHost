import re


def test_hid_keyboard_nkro_detected(dut, peers):
    device = peers["device"]
    # '?' handshake so readiness does not depend on capture starting before boot.
    device.write("?")
    device.expect(r"DEVICE_READY nkro=1")

    dut.expect_exact("HOST_CONNECTED")
    dut.write("i")
    # The host must recognize the NKRO bitmap report from the HID report descriptor.
    dut.expect_exact("NKRO bitmap=1")


def test_hid_keyboard_nkro_chord(dut, peers):
    device = peers["device"]
    device.write("?")
    device.expect(r"DEVICE_READY nkro=1")

    dut.write("r")
    dut.expect_exact("RESET")

    device.write("c")
    device.expect(r"SENT_CHORD n=8 protocol=report")

    # All eight keys must be reported held at the same time — impossible with the
    # 6-key boot report, so this is the NKRO proof.
    dut.expect_exact("STATE down=8")
    dut.expect(r"PRESS keycode=0x[0-9a-f]+ n=8", timeout=10)

    dut.write("m")
    dut.expect(r"MAX n=8")


def test_hid_keyboard_nkro_led(dut, peers):
    device = peers["device"]
    device.write("?")
    device.expect(r"DEVICE_READY nkro=1")

    # LEDs must reach the keyboard while NKRO (report protocol) is active.
    dut.write("l")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=1 scrolllock=0")

    dut.write("o")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=0 scrolllock=0")
