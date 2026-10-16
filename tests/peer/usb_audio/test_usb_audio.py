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
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x01 dir=OUT channels=1 bytes=2 bits=16 rate=32000 rates=1 first=32000 min=0 max=0 maxPacket=98 interval=1")
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=32000 rates=1 first=32000 min=0 max=0 maxPacket=98 interval=1")

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

    # Replace the push smoke test with a finite pull source for the delivery
    # check. Silence after the source must add bytes without changing its sum.
    dut.write("o")
    dut.expect_exact("AUDIO_OUT_STOP")
    time.sleep(0.1)
    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")
    dut.write("b")
    dut.expect_exact("AUDIO_OUT_START 1")
    dut.write("r")
    dut.expect_exact("AUDIO_RESET")
    device.write("M")
    sent = device.expect(r"DEVICE_AUDIO_STREAM bytes=(\d+) ms=(\d+)\r?\n", timeout=10)
    time.sleep(0.1)
    dut.write("q")
    capture = dut.expect(r"AUDIO_CAPTURE bytes=(\d+) sequence_errors=(\d+) running=1\r?\n")
    received = int(capture.group(1))
    print(f"\ncapture: {received} bytes / {int(sent.group(2))} ms; "
          f"peer accepted {int(sent.group(1))} bytes; sequence errors={int(capture.group(2))}")
    device.write("q")
    output = device.expect(r"DEVICE_AUDIO_RX bytes=(\d+) sum=(\d+)\r?\n")
    print(f"pull playback: {int(output.group(1))} received bytes, sum={int(output.group(2))}")

    # Stop must drain callbacks, survive repetition, and allow a fresh start.
    dut.write("t")
    dut.expect_exact("AUDIO_IN_STOP 1 running=0")
    device.expect_exact("AUDIO_INTERFACE MIC 0")
    dut.write("q")
    stopped = dut.expect(r"AUDIO_CAPTURE bytes=(\d+) sequence_errors=\d+ running=0\r?\n")
    device.write("m")
    device.expect(r"DEVICE_TX_AUDIO \d+\r?\n")
    time.sleep(0.1)
    dut.write("q")
    idle = dut.expect(r"AUDIO_CAPTURE bytes=(\d+) sequence_errors=\d+ running=0\r?\n")
    assert idle.group(1) == stopped.group(1), "capture callbacks continued after stop"
    dut.write("t")
    dut.expect_exact("AUDIO_IN_STOP 1 running=0")
    dut.write("r")
    dut.expect_exact("AUDIO_RESET")
    dut.write("i")
    dut.expect_exact("AUDIO_IN_START 1")
    device.write("m")
    device.expect(r"DEVICE_TX_AUDIO [1-9]\d*\r?\n")
    dut.expect(r"AUDIO_RX addr=\d+ iface=\d+ total=[1-9]\d* last=[1-9]\d*\r?\n")

    # Connection callbacks use the same client-task path. Verify that playback
    # deferred from a USB callback also preserves the first packet of the source.
    dut.write("o")
    dut.expect_exact("AUDIO_OUT_STOP")
    time.sleep(0.1)
    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")
    dut.write("c")
    dut.expect_exact("AUDIO_OUT_CALLBACK_ARMED")
    device.write("m")
    dut.expect_exact("AUDIO_OUT_START 1 callback=1")
    device.expect(r"DEVICE_TX_AUDIO [1-9]\d*\r?\n")
    time.sleep(0.3)
    device.write("q")
    callback_output = device.expect(r"DEVICE_AUDIO_RX bytes=(\d+) sum=(\d+)\r?\n")
    print(f"callback pull playback: {int(callback_output.group(1))} bytes, "
          f"sum={int(callback_output.group(2))}")
    dut.write("t")
    dut.expect_exact("AUDIO_IN_STOP 1 running=0")
    assert received > 0
    assert int(capture.group(2)) == 0, "capture lost or reordered PCM samples"
    assert int(output.group(1)) >= 4096 * 2
    assert int(output.group(2)) == 4096 * 4097 // 2, "pull playback lost source samples"
    assert int(callback_output.group(2)) == 4096 * 4097 // 2, "callback-started playback lost source samples"
