import time

import pytest


def _poll_state(dut, pattern, attempts=100):
    """Wait for a state the sketch answers on demand.

    Polling a query rather than waiting for a connect line matters because a
    connect line is printed once, when the device enumerates, so waiting for it
    only works while the test doing so happens to run first.
    """
    for _ in range(attempts):
        dut.write("Q")
        try:
            dut.expect(pattern, timeout=2)
            return
        except Exception:
            continue
    raise AssertionError(f"the host never reported {pattern!r}")


@pytest.fixture(autouse=True)
def usb_host(dut, peers):
    """Start the USB host for this test, and stop it however the test ends.

    The sketch does not start it in setup(): the peer board is flashed after this
    board has booted, and a host that is already running observes those resets
    and records the enumerations they cause as errors.

    `peers` is requested for its ordering, not its value. This fixture is autouse
    and would otherwise be set up before it, which starts the host before the peer
    upload -- putting the host back inside exactly the window the gating exists to
    avoid. Requesting `peers` moves the upload ahead of the start.

    Stopping in the teardown rather than at the end of the test body means it
    runs when the test fails or is interrupted too, so the board is not left
    hosting USB after the run.
    """
    _poll_state(dut, r"HOST_STATE (?:idle|running) devices=\d+")
    dut.write("G")
    # Wait for enumeration. This module's tests do not read the connect-time
    # output, so polling here consumes nothing they need.
    _poll_state(dut, r"HOST_STATE running devices=[1-9]")
    yield
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")




def _set_leds(dut, command):
    # The LED target is learned from the HID report descriptor slightly after
    # HOST_CONNECTED, so retry until the host accepts the request.
    for _ in range(50):
        dut.write(command)
        if dut.expect(r"LED_TX (\d)", timeout=5).group(1) == b"1":
            return
        time.sleep(0.1)
    raise AssertionError("setKeyboardLeds() kept failing")


def test_hid_keyboard_composite(dut, peers, run_checks):
    def led():
        device = peers["device"]
        # '?' handshake so readiness does not depend on capture starting before boot.
        # (The host also pushes its lock state right after the descriptor is parsed,
        # but that can happen before capture starts, so it is not asserted here.)
        device.write("?")
        device.expect(r"DEVICE_READY ready=1")

        # Host readiness is not asserted here. HOST_CONNECTED is printed once, when
        # the device enumerates, so waiting for it would only work while this is the
        # first test in the module -- add a test above it and this one starts failing.
        # _set_leds() already polls until the host accepts the request, which is the
        # order-independent way to wait.

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

    def input():
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

    run_checks([
        led,
        input,
    ])
