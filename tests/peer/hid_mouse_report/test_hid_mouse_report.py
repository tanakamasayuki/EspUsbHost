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


"""Report-protocol mouse: a peer that declares 16 buttons, 16-bit X/Y, a wheel
and AC Pan in an 8-byte report with no report ID, which is the layout issue #39
reports for a Logitech G502 HERO.

Decoded as a boot mouse report (what the library did before the report
descriptor was parsed) every one of these assertions fails: the button high byte
lands in X, the X low byte in Y and the X high byte in the wheel, and a report
that only moves Y produces no event at all because the three bytes the boot
layout reads are zero.
"""


def test_hid_mouse_report(dut, peers, run_checks):
    def axes():
        device = peers["device"]

        # 16-bit deltas, well past what an 8-bit boot axis can carry.
        device.write("r")
        dut.expect_exact("MOUSE x=300 y=0 wheel=0 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

        device.write("l")
        dut.expect_exact("MOUSE x=-300 y=0 wheel=0 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

        # The regression from the issue: a Y-only report must produce an event, and
        # must not leak into the wheel.
        device.write("d")
        dut.expect_exact("MOUSE x=0 y=300 wheel=0 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

        device.write("u")
        dut.expect_exact("MOUSE x=0 y=-300 wheel=0 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

    def wheel_and_pan():
        device = peers["device"]

        device.write("w")
        dut.expect_exact("MOUSE x=0 y=0 wheel=1 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

        device.write("p")
        dut.expect_exact("MOUSE x=0 y=0 wheel=0 pan=-1 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

    def buttons():
        device = peers["device"]

        device.write("m")
        dut.expect_exact("buttons=1 mask=1 previous=0 count=16 moved=0 changed=1")
        dut.expect_exact("buttons=0 mask=0 previous=1 count=16 moved=0 changed=1")

        device.write("R")
        dut.expect_exact("buttons=2 mask=2 previous=0 count=16 moved=0 changed=1")
        dut.expect_exact("buttons=0 mask=0 previous=2 count=16 moved=0 changed=1")

        # Button 16 lives in the high byte of the mask: invisible in `buttons`, which
        # keeps the low 8 for compatibility, and invisible entirely under the boot
        # layout, which reads that byte as X.
        device.write("x")
        dut.expect_exact("buttons=0 mask=32768 previous=0 count=16 moved=0 changed=1")
        dut.expect_exact("buttons=0 mask=0 previous=32768 count=16 moved=0 changed=1")

    run_checks([
        axes,
        wheel_and_pan,
        buttons,
    ])
