def test_hid_keyboard_composite_led(dut, peers):
    device = peers["device"]
    # '?' handshake so readiness does not depend on capture starting before boot.
    device.write("?")
    device.expect(r"DEVICE_READY ready=1")

    dut.expect_exact("HOST_CONNECTED")

    # The host pushes its lock state (Num Lock on by default) as soon as the LED
    # output report is learned from the report descriptor — no boot interface is
    # involved, so this only works via the report-descriptor path.
    device.expect_exact("LED numlock=1 capslock=0 scrolllock=0")

    dut.write("c")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=1 scrolllock=0")

    dut.write("s")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=0 scrolllock=1")

    dut.write("o")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=0 scrolllock=0")


def test_hid_keyboard_composite_input(dut, peers):
    device = peers["device"]
    device.write("?")
    device.expect(r"DEVICE_READY ready=1")

    # Keyboard, consumer control, and mouse all share one interface with report
    # IDs; each report must still reach its own host callback.
    device.write("k")
    dut.expect_exact("KEY k")

    device.write("v")
    dut.expect(r"CONSUMER usage=0x00e9 pressed=1")
    dut.expect(r"CONSUMER usage=0x0000 pressed=0")

    device.write("m")
    dut.expect_exact("MOUSE x=40 y=0")
