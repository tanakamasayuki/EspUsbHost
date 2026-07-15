import time


def _set_leds(dut, command):
    # The LED target is learned from the HID report descriptor slightly after
    # HOST_CONNECTED, so retry until the host accepts the request.
    for _ in range(50):
        dut.write(command)
        if dut.expect(r"LED_TX (\d)", timeout=5).group(1) == b"1":
            return
        time.sleep(0.1)
    raise AssertionError("setKeyboardLeds() kept failing")


def test_hid_keyboard_composite_led(dut, peers):
    device = peers["device"]
    # '?' handshake so readiness does not depend on capture starting before boot.
    # (The host also pushes its lock state right after the descriptor is parsed,
    # but that can happen before capture starts, so it is not asserted here.)
    device.write("?")
    device.expect(r"DEVICE_READY ready=1")

    dut.expect_exact("HOST_CONNECTED")

    # Num Lock alone puts LED byte 0x01 on the wire — the same value as the
    # keyboard's report ID — so this also pins the report-ID-prefixed payload
    # (TinyUSB strips a leading payload byte that equals the report ID).
    _set_leds(dut, "n")
    device.expect_exact("LED numlock=1 capslock=0 scrolllock=0")

    _set_leds(dut, "c")
    device.expect_exact("LED numlock=0 capslock=1 scrolllock=0")

    _set_leds(dut, "s")
    device.expect_exact("LED numlock=0 capslock=0 scrolllock=1")

    _set_leds(dut, "o")
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
    # The host reports the release with the usage that was let go, not usage 0.
    dut.expect(r"CONSUMER usage=0x00e9 pressed=1")
    dut.expect(r"CONSUMER usage=0x00e9 pressed=0")

    device.write("m")
    dut.expect_exact("MOUSE x=40 y=0")
