import time


STATS = (
    r"{tag} ready=(\d) link=(\d) netif=(\d) rxNtb=(\d+) rxFrames=(\d+) "
    r"tx=(\d+) txFail=(\d+) oversized=(\d+) ntbIn=(\d+) heap=(\d+) block=(\d+)"
)

# Field indices into the STATS groups.
RX_NTB, RX_FRAMES, TX_FRAMES, TX_FAILS, OVERSIZED, NTB_IN = 4, 5, 6, 7, 8, 9


def _wait_device_link(device, timeout=15):
    deadline = time.monotonic() + timeout
    while True:
        device.write("?")
        match = device.expect(
            r"DEVICE_READY ip=192\.168\.7\.1 link=(\d) ntbIn=(\d+)",
            timeout=min(2, max(0.1, deadline - time.monotonic())),
        )
        if int(match.group(1)) == 1:
            return int(match.group(2))
        if time.monotonic() >= deadline:
            raise AssertionError(f"USB NCM device link did not come up within {timeout}s")
        time.sleep(0.1)


def _stats(dut, tag, timeout=10):
    return dut.expect(STATS.format(tag=tag), timeout=timeout)


def test_usb_ncm_throughput(dut, peers):
    """Sustained traffic in both directions over the USB NIC.

    The peer advertises a dwNtbInMaxSize larger than the host's preferred
    receive buffer (see peer_device/build_opt.h) and writes in bursts, so it
    batches several datagrams per NTB the way a real USB NIC does under load.
    Before the host negotiated the NTB input size, every NTB above its fixed
    3200-byte buffer was dropped whole and device->host throughput collapsed
    to ~98 kbps with multi-second TCP stalls, while the link still looked up
    and txFail stayed 0.

    Which negotiation branch runs depends on the peer: this one advertises
    bmNetworkCapabilities = 0, i.e. no SET_NTB_INPUT_SIZE support, so the host
    has to size its receive buffer after the device's dwNtbInMaxSize. The
    SET_NTB_INPUT_SIZE branch needs a device that advertises capability bit 3
    and is not covered here.
    """
    device = peers["device"]
    # Informational only. This is the peer sketch's own CFG_TUD_NCM_IN_NTB_MAX_SIZE
    # macro, which disagrees with what the device advertises on the wire whenever
    # arduino-cli reuses a cached EspUsbDevice build that predates build_opt.h --
    # the host's negotiated size below is the authority.
    device_ntb_in = _wait_device_link(device)

    dut.expect_exact("HOST_CONNECTED")
    dut.write("a")
    dut.expect_exact("NETWORK_ATTACH ok=1")
    dut.expect(r"ip=192\.168\.7\.\d", timeout=30)

    # host -> device (bulk OUT).
    dut.write("t")
    tx = dut.expect(
        r"TX_SOAK connect=1 bytes=(\d+) ms=(\d+) kbps=(\d+) writeFails=(\d+)", timeout=30
    )
    tx_stats = _stats(dut, "TX_SOAK_STATS")

    # device -> host (bulk IN), where the batched NTBs arrive.
    dut.write("r")
    rx = dut.expect(
        r"RX_SOAK connect=1 bytes=(\d+) ms=(\d+) kbps=(\d+) maxIdleMs=(\d+)", timeout=30
    )
    rx_stats = _stats(dut, "RX_SOAK_STATS")

    device.write("c")
    counts = device.expect(r"DEVICE_COUNTS sink=(\d+) source=(\d+)", timeout=10)

    print(f"device dwNtbInMaxSize={device_ntb_in}")
    print("TX:", tx.group(0))
    print("TX stats:", tx_stats.group(0))
    print("RX:", rx.group(0))
    print("RX stats:", rx_stats.group(0))
    print("device counts:", counts.group(0))

    tx_bytes, rx_bytes = int(tx.group(1)), int(rx.group(1))
    negotiated = int(rx_stats.group(NTB_IN))
    oversized = int(rx_stats.group(OVERSIZED))
    rx_ntb = int(rx_stats.group(RX_NTB))
    rx_frames = int(rx_stats.group(RX_FRAMES))

    # The negotiated size comes from the device's GET_NTB_PARAMETERS answer, so
    # it is what the peer really advertises. It has to exceed the host's former
    # fixed 3200 for this test to exercise anything: a smaller value means the
    # peer binary predates build_opt.h, which arduino-cli happily reuses from
    # peer_device/build even after the flags change.
    assert negotiated > 3200, (
        f"peer advertises only {negotiated} bytes (its sketch reports {device_ntb_in}); "
        "re-run with --clean so the peer's build_opt.h takes effect for the cached "
        "EspUsbDevice build too"
    )
    # A multiple of the bulk IN max packet size (64 on this full-speed peer),
    # because ESP-IDF specifies IN transfer lengths as integer multiples of MPS.
    assert negotiated % 64 == 0, f"negotiated NTB size {negotiated} is not MPS aligned"

    # Nothing may be dropped for being too large: either the device honoured
    # SET_NTB_INPUT_SIZE, or the buffer followed its maximum.
    assert oversized == 0, f"{oversized} NTBs dropped as oversized (negotiated {negotiated})"

    # The peer really did batch, so the oversized check above was exercised.
    assert rx_frames > rx_ntb, (
        f"peer did not batch datagrams (rxNtb={rx_ntb}, rxFrames={rx_frames}); "
        "the regression this test guards would not be visible"
    )

    assert tx_bytes > 1_000_000, f"host->device throughput collapsed: {tx_bytes} bytes in 5s"
    assert rx_bytes > 1_000_000, f"device->host throughput collapsed: {rx_bytes} bytes in 5s"
    assert int(tx_stats.group(TX_FAILS)) == 0, "bulk OUT reported failures"

    # Still alive after the soak.
    dut.write("d")
    after = _stats(dut, "NETWORK_STATS")
    print("after:", after.group(0))
    assert int(after.group(1)) == 1 and int(after.group(2)) == 1, "link went down during the soak"
    assert int(after.group(OVERSIZED)) == 0, "oversized NTBs after the soak"
