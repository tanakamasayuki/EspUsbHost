import time

import pytest


def _poll_state(dut, pattern, attempts=100):
    """Wait for a state the sketch answers on demand.

    Same reason as tests/peer/usb_vendor: a connect line is printed once, so
    waiting for it only works while the test doing so happens to run first.
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
    """Start the USB host for this test, and stop it however the test ends."""
    _poll_state(dut, r"HOST_STATE (?:idle|running) devices=\d+")
    dut.write("G")
    yield
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")


def _info(dut, timeout=2):
    dut.write("i")
    match = dut.expect(
        r"VENDOR_INFO connected=(\d) in_ep=0x([0-9a-f]{2}) in_mps=(\d+) xfer=(\d+) "
        r"queue=(\d) pending=(\d+)",
        timeout=timeout,
    )
    return {
        "connected": int(match.group(1)),
        "in_ep": int(match.group(2), 16),
        "mps": int(match.group(3)),
        "xfer": int(match.group(4)),
        "queue": int(match.group(5)),
        "pending": int(match.group(6)),
    }


def _wait_ready(dut, device, timeout=20):
    """Wait until the peer has enumerated.

    Only `connected` can be polled here. `in_ep` and `mps` come from
    vendorInEndpoint() / vendorInPacketSize(), which report the endpoint the
    interface was opened on -- so they stay 0 until a check calls vendorOpen(),
    and waiting for them here would wait forever.
    """
    device.write("?")
    device.expect_exact("DEVICE_READY")

    deadline = time.monotonic() + timeout
    while True:
        info = _info(dut, timeout=min(2, max(0.1, deadline - time.monotonic())))
        if info["connected"] == 1:
            return info
        if time.monotonic() >= deadline:
            raise AssertionError(f"the vendor peer did not become ready within {timeout}s")
        time.sleep(0.1)


def _restart(dut, device):
    """Put the host back to a state where the read mode and size are unset.

    Both are fixed when the interface is opened, so a check that opens it one way
    would otherwise decide what the next check is allowed to do.
    """
    dut.write("x")
    dut.expect_exact("HOST_REBEGIN 1")
    return _wait_ready(dut, device)


def _stream(dut, timeout=60):
    dut.write("n")
    match = dut.expect(
        r"VENDOR_STREAM mode=(\w+) requested=(\d+) bytes=(\d+) chunks=(\d+) max_chunk=(\d+) "
        r"bad=(\d+) elapsed_us=(\d+) mbps=([0-9.]+) submitted=(\d+) completed=(\d+) "
        r"errors=(\d+) short=(\d+) starved=(\d+) queue_bytes=(\d+)",
        timeout=timeout,
    )
    fields = [match.group(i) for i in range(1, 15)]
    return {
        "mode": fields[0].decode(),
        "requested": int(fields[1]),
        "bytes": int(fields[2]),
        "chunks": int(fields[3]),
        "max_chunk": int(fields[4]),
        "bad": int(fields[5]),
        "elapsed_us": int(fields[6]),
        "mbps": float(fields[7]),
        "submitted": int(fields[8]),
        "completed": int(fields[9]),
        "errors": int(fields[10]),
        "short": int(fields[11]),
        "starved": int(fields[12]),
        "queue_bytes": int(fields[13]),
    }


def test_usb_vendor_read(dut, peers, run_checks):
    device = peers["device"]

    def default_reads_one_packet():
        """Without a size, a continuous read is still one endpoint packet."""
        _restart(dut, device)
        dut.write("o")
        match = dut.expect(r"VENDOR_OPEN ok=1 xfer=(\d+) mps=(\d+)")
        assert match.group(1) == match.group(2), "the default transfer should be one max-size packet"

        # Opening is what makes the endpoint observable, so check it here rather
        # than while waiting for the device to enumerate.
        info = _info(dut)
        assert info["in_ep"] != 0, "vendorOpen() should report the bulk IN endpoint it took"
        assert info["mps"] == int(match.group(2))

    def large_transfer_size():
        """vendorOpen() sizes the continuous transfer, and will not resize it."""
        _restart(dut, device)
        dut.write("O")
        dut.expect_exact("VENDOR_OPEN_BIG ok=1 xfer=8192")

        # Re-opening with another size has to fail: the transfer is allocated once.
        dut.write("C")
        dut.expect_exact("VENDOR_OPEN_CONFLICT ok=0 xfer=8192")
        assert _info(dut)["xfer"] == 8192

    def stream_over_large_transfers():
        """A stream read in large transfers arrives whole and in order."""
        _restart(dut, device)
        dut.write("O")
        dut.expect_exact("VENDOR_OPEN_BIG ok=1 xfer=8192")

        result = _stream(dut)
        assert result["bytes"] == result["requested"]
        assert result["bad"] == 0, "the ramp must arrive unbroken"
        info = _info(dut)
        assert result["max_chunk"] > info["mps"], (
            "a transfer larger than one packet should deliver more than one packet at a time"
        )

    def read_queue_carries_the_stream():
        """The async queue reads the same stream with several transfers in flight."""
        _restart(dut, device)
        dut.write("O")
        dut.expect_exact("VENDOR_OPEN_BIG ok=1 xfer=8192")

        dut.write("q")
        dut.expect_exact("VENDOR_RQ_BEGIN ok=1 ready=1 xfer=8192 pending=2")

        # The queue owns the endpoint, so a synchronous read must be refused
        # rather than wait for an answer the queue has already taken.
        dut.write("S")
        dut.expect(r"VENDOR_READ_SYNC ok=0 error=-?\d+")

        result = _stream(dut)
        assert result["mode"] == "queue"
        assert result["bytes"] == result["requested"]
        assert result["bad"] == 0, "the ramp must arrive unbroken"
        assert result["errors"] == 0
        assert result["completed"] > 0
        assert result["queue_bytes"] == result["bytes"], (
            "the queue's byte counter should agree with what the callback saw"
        )

        dut.write("e")
        dut.expect_exact("VENDOR_RQ_END ready=0 pending=0")

    def queue_rejects_invalid_shapes():
        _restart(dut, device)
        dut.write("O")
        dut.expect_exact("VENDOR_OPEN_BIG ok=1 xfer=8192")
        dut.write("Z")
        dut.expect_exact("VENDOR_RQ_REJECT depth0=0 deep=0 bytes0=0")

    def read_windows_are_contiguous():
        """One vendorRead() never returns a window spliced from two places.

        The stream is far larger than the receive ring, so the ring overflows the
        whole time and bytes are lost between reads -- that is what the ring
        documents. The overflow path discards the oldest bytes by advancing the
        tail, which is the index vendorRead() is walking, so without mutual
        exclusion a read in progress can have unrelated bytes spliced into the
        middle of the window it returns. The peer sends an unbroken 0..255 ramp,
        so a seam inside one window is a byte that is not one more than the byte
        before it.

        Measured both ways on the full-speed S3 pair: without the locking it
        reports about 42 seams in 3,286 windows, with it exactly zero, on an
        identical workload (65,536 pushes either way). The asymmetry is what
        makes it usable as a regression test -- a seam is only possible while the
        producer can move the consumer's tail, so a fixed build cannot produce
        one, while an unfixed build produces dozens.

        Three things had to line up before it reproduced at all, and getting any
        of them wrong hides the fault completely:

        * The read has to be short. Draining the whole ring frees space as the
          copy proceeds, so a push arriving midway takes no drop and moves no
          tail.
        * The reader has to pause. Reading in a tight loop keeps the ring from
          filling, and the overflow path only runs when it is full.
        * There have to be enough windows. At roughly 1.3% per window, the
          1,500 ms run this started as produced one seam or none.
        """
        _restart(dut, device)
        # Deliberately the default one-packet transfer, not the 8 KB one. A push
        # at least as large as the ring takes the other overflow branch, which
        # resets the ring wholesale instead of advancing the tail, and that
        # branch cannot splice a window. Only pushes smaller than the ring reach
        # the partial-drop path this check is about.
        dut.write("o")
        dut.expect(r"VENDOR_OPEN ok=1 xfer=(\d+) mps=(\d+)")

        dut.write("W")
        match = dut.expect(r"VENDOR_WINDOW windows=(\d+) bytes=(\d+) seams=(\d+)", timeout=30)
        windows = int(match.group(1))
        seams = int(match.group(3))
        assert windows > 0, "no windows were read; the peer did not stream"
        assert seams == 0, (
            f"{seams} seam(s) inside {windows} window(s): vendorRead() returned bytes "
            "spliced from two places in the ring"
        )

    run_checks([
        default_reads_one_packet,
        large_transfer_size,
        stream_over_large_transfers,
        read_queue_carries_the_stream,
        queue_rejects_invalid_shapes,
        read_windows_are_contiguous,
    ])
