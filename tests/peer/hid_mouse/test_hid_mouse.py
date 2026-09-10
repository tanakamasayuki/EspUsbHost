import pytest


@pytest.fixture(autouse=True)
def usb_host(dut, peers):
    """Start the USB host for this test and stop it afterwards.

    The sketch does not start it in setup(): the peer is flashed after this board
    has booted, and a host that is already running observes those resets.

    `peers` is requested for its ordering, not its value. This fixture is autouse
    and would otherwise be set up before it, which starts the host before the peer
    upload -- putting the host back inside exactly the window the gating exists to
    avoid. Requesting `peers` moves the upload ahead of the start.

    Stopping in the teardown rather than at the end of the test body means it
    also runs when the test fails or is interrupted, so the board is not left
    hosting USB after the run.
    """
    _poll_state(dut, r"HOST_STATE (?:idle|running) devices=\d+")
    dut.write("G")
    # Wait for the device to be enumerated. This module's checks read only the
    # MOUSE lines that follow, so polling here consumes nothing they need -- a
    # module whose checks read the connect-time output must not do this.
    _poll_state(dut, r"HOST_STATE running devices=[1-9]")
    yield
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")


def _poll_state(dut, pattern, attempts=100):
    for _ in range(attempts):
        dut.write("Q")
        try:
            dut.expect(pattern, timeout=2)
            return
        except Exception:
            continue
    raise AssertionError(f"the host never reported {pattern!r}")


def test_hid_mouse(dut, peers, run_checks):
    def move():
        device = peers["device"]

        device.write("r")
        dut.expect_exact("MOUSE x=40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")
        dut.expect_exact("MOUSE_LISTENER x=40 y=0 buttons=0")

        device.write("l")
        dut.expect_exact("MOUSE x=-40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

        device.write("d")
        dut.expect_exact("MOUSE x=0 y=40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

        device.write("u")
        dut.expect_exact("MOUSE x=0 y=-40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

        device.write("w")
        dut.expect_exact("MOUSE x=0 y=0 wheel=1 buttons=0 previous=0 moved=1 changed=0")

    def buttons():
        device = peers["device"]

        device.write("m")
        dut.expect_exact("buttons=1 previous=0 moved=0 changed=1")
        dut.expect_exact("buttons=0 previous=1 moved=0 changed=1")

        device.write("R")
        dut.expect_exact("buttons=2 previous=0 moved=0 changed=1")
        dut.expect_exact("buttons=0 previous=2 moved=0 changed=1")

        device.write("M")
        dut.expect_exact("buttons=4 previous=0 moved=0 changed=1")
        dut.expect_exact("buttons=0 previous=4 moved=0 changed=1")

        device.write("b")
        dut.expect_exact("buttons=8 previous=0 moved=0 changed=1")
        dut.expect_exact("buttons=0 previous=8 moved=0 changed=1")

        device.write("f")
        dut.expect_exact("buttons=16 previous=0 moved=0 changed=1")
        dut.expect_exact("buttons=0 previous=16 moved=0 changed=1")

    run_checks([
        move,
        buttons,
    ])
