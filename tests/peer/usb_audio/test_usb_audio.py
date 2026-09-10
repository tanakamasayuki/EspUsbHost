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
    # Deliberately no wait here: this module's tests read the connect-time output
    # themselves, and an expect() in this fixture would advance the reader past
    # it. Each test now gets a fresh begin(), so that output is printed per test
    # instead of only once at boot.
    yield
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")


def test_usb_audio_bidirectional(dut, peers):
    device = peers["device"]

    device.expect_exact("AUDIO_DEVICE_READY")
    dut.expect("AUDIO_IN_READY addr=[0-9]+\\r?\\n")
    dut.expect("AUDIO_OUT_READY addr=[0-9]+\\r?\\n")
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x01 dir=OUT channels=1 bytes=2 bits=16 rate=48000 rates=1 first=48000 min=0 max=0 maxPacket=98 interval=1")
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 first=48000 min=0 max=0 maxPacket=98 interval=1")

    dut.write("a")
    dut.expect_exact("AUDIO_OUT_START 1")
    device.expect_exact("AUDIO_INTERFACE SPK 1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")

    dut.write("s")
    dut.expect("AUDIO_TX [1-9][0-9]*\\r?\\n")
    device.expect("DEVICE_RX_AUDIO [1-9][0-9]*\\r?\\n")

    dut.write("i")
    dut.expect_exact("AUDIO_IN_START 1")
    device.expect_exact("AUDIO_INTERFACE MIC 1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("m")
    device.expect("DEVICE_TX_AUDIO [1-9][0-9]*\\r?\\n")
    dut.expect("AUDIO_RX addr=[0-9]+ iface=[0-9]+ total=[1-9][0-9]* last=[1-9][0-9]*\\r?\\n")
