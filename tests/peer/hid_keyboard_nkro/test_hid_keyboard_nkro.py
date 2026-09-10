import re
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




def _wait_nkro_bitmap(dut, attempts=50):
    """Wait until the host reports the NKRO bitmap report from the descriptor."""
    for _ in range(attempts):
        dut.write("i")
        if dut.expect(r"NKRO bitmap=(\d)", timeout=5).group(1) == b"1":
            return
        time.sleep(0.1)
    raise AssertionError("the host never recognized the NKRO bitmap report")


def test_hid_keyboard_nkro(dut, peers, run_checks):
    def detected():
        device = peers["device"]
        # '?' handshake so readiness does not depend on capture starting before boot.
        device.write("?")
        device.expect(r"DEVICE_READY nkro=1")

        # Poll rather than waiting for HOST_CONNECTED: that line is printed once, when
        # the device enumerates, so waiting for it only works while this is the first
        # test in the module. The query answers "bitmap=0" until the host has parsed
        # the report descriptor, so retrying is what makes this independent of order.
        _wait_nkro_bitmap(dut)

    def chord():
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

    def led():
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

    run_checks([
        detected,
        chord,
        led,
    ])
