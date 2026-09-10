import time

from pexpect.exceptions import TIMEOUT

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


def test_usb_serial(dut, peers, run_checks):
    def device_to_host():
        device = peers["device"]

        device.write("d")
        dut.expect_exact("SERIAL_RX device to host")

    def host_to_device():
        device = peers["device"]

        dut.write("h")
        dut.expect_exact("SERIAL_TX 1")
        device.expect_exact("DEVICE_RX host to serial")

    def config_api():
        device = peers["device"]

        dut.write("c")
        dut.expect_exact("SERIAL_CONFIG 1")
        device.write("l")
        device.expect_exact("DEVICE_LINE_CODING seen=1 baud=57600 stop=2 parity=2 data=7")
        dut.write("h")
        dut.expect_exact("SERIAL_TX 1")
        device.expect_exact("DEVICE_RX host to serial")

        dut.write("m")
        dut.expect_exact("SERIAL_CONFIG_MARK 1")
        device.write("l")
        device.expect_exact("DEVICE_LINE_CODING seen=1 baud=300 stop=1 parity=3 data=5")

        dut.write("b")
        dut.expect_exact("SERIAL_BAUD 1")
        device.write("l")
        device.expect_exact("DEVICE_LINE_CODING seen=1 baud=115200 stop=1 parity=3 data=5")

    def write_queue():
        """The asynchronous CDC OUT queue, whose pool is allocated per port.

        Covers begin, the zero-copy acquire/submit pair, the copying async path, a
        plain Stream write routing through the active queue, flush, the statistics
        counters, and release. Without this the queue had no automated coverage at
        all -- its only other user is the manual USB display example.
        """
        device = peers["device"]

        dut.write("q")
        dut.expect_exact("QUEUE_BEGIN 1 ready=1 free=4")
        dut.expect_exact("QUEUE_ACQUIRE 1 capacity=128 submit=1")
        dut.expect_exact("QUEUE_ASYNC 1")
        dut.expect_exact("QUEUE_STREAM 1")
        dut.expect_exact("QUEUE_FLUSH 1")
        # Three transfers went out: the acquired slot, the async copy, and the Stream
        # write that the active queue took over.
        dut.expect_exact("QUEUE_STATS submitted=3 completed=3 errors=0")
        dut.expect_exact("QUEUE_END ready=0")
        dut.expect_exact("QUEUE_AFTER 1")

        device.expect_exact("QACQ")
        device.expect_exact("QASYNC")
        device.expect_exact("QWRITE")
        device.expect_exact("QAFTER")

    def write_queue_cycles():
        """Repeated begin/end of the per-port write queue must not leak its heap block.

        The transfer pool, semaphore and statistics moved off SerialPortState onto the
        heap, which makes the release path the thing that can now leak. A single
        begin/end pair would not show it; twenty will, because one leaked cycle is the
        queue block plus its four 128-byte DMA transfers.
        """
        device = peers["device"]

        dut.write("r")
        cycles = dut.expect(
            r"QUEUE_CYCLES completed=([0-9]+) before=([0-9]+) after=([0-9]+) delta=(-?[0-9]+) ready=([0-9]+)\r?\n",
            timeout=30)
        assert int(cycles.group(1)) == 20, "a begin/write/flush/end cycle failed part way through"
        assert int(cycles.group(5)) == 0, "the queue was still ready after serialWriteQueueEnd()"
        delta = int(cycles.group(4))
        # Twenty leaked cycles would be over 10 KB, so this is nowhere near the noise
        # floor; the slack is for allocator bookkeeping, not for a small leak.
        assert delta > -512, f"free heap fell by {-delta} bytes over 20 queue cycles"

        device.expect_exact("QCYC")

    def end_rebegin_with_device_open():
        device = peers["device"]

        dut.write("x")
        dut.expect_exact("HOST_END installed=0 clients=-1 devices=-1 ready=0")
        dut.expect_exact("HOST_REBEGIN 1")

        deadline = time.monotonic() + 15
        while True:
            device.write("d")
            try:
                dut.expect_exact("SERIAL_RX device to host", timeout=1)
                break
            except TIMEOUT:
                if time.monotonic() >= deadline:
                    raise AssertionError("CDC device did not resume after host restart")

        dut.write("h")
        dut.expect_exact("SERIAL_TX 1")
        device.expect_exact("DEVICE_RX host to serial")

    run_checks([
        device_to_host,
        host_to_device,
        config_api,
        write_queue,
        write_queue_cycles,
        end_rebegin_with_device_open,
    ])
