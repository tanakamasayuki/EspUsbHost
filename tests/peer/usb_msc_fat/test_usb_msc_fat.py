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


def test_usb_msc_fat(dut, peers, run_checks):
    def peer_volume_is_formatted():
        """The peer formats its RAM disk with FatFs before USB comes up."""
        device = peers["device"]
        device.write("s")
        device.expect_exact("DEVICE_FAT_STATUS formatted=1 blocks=256 block_size=512")

    def mount_read_unmount():
        dut.write("m")
        dut.expect_exact("FAT_MOUNT ok=1 mounted=1 error=ESP_OK")
        dut.write("r")
        dut.expect_exact("FAT_READ ok=1 data=MSC_FAT_PEER")
        dut.write("u")
        dut.expect_exact("FAT_UNMOUNT ok=1 mounted=0 error=ESP_OK")

    def unmount_then_end_then_rebegin():
        """The reported sequence: mount, unmount, end(), begin(), mount again."""
        dut.write("c")
        dut.expect_exact("CYCLE_MOUNT ok=1")
        dut.expect_exact("CYCLE_READ ok=1 data=MSC_FAT_PEER")
        dut.expect_exact("CYCLE_UNMOUNT ok=1 error=ESP_OK")
        dut.expect_exact("CYCLE_STOP mounted=0 error=ESP_OK")
        dut.expect_exact("CYCLE_RESTART ready=1 error=ESP_OK", timeout=20)
        dut.expect_exact("CYCLE_REMOUNT ok=1", timeout=20)
        dut.expect_exact("CYCLE_REREAD ok=1 data=MSC_FAT_PEER")
        dut.expect_exact("CYCLE_FINAL_UNMOUNT ok=1")

    def end_releases_a_still_mounted_volume():
        """end() must drop the mounts it owns, not just the USB side.

        A mount holds a FatFs drive slot and a registered VFS path, and the mount
        table has FF_VOLUMES (2) entries. Leaving one behind makes the next
        mscMount() of the same basePath fail with "basePath already mounted", and
        the drive slots run out after two cycles.
        """
        dut.write("k")
        dut.expect_exact("KEEP_MOUNT ok=1")
        dut.expect_exact("KEEP_STOP mounted=0 error=ESP_OK")
        dut.expect_exact("KEEP_RESTART ready=1 error=ESP_OK", timeout=20)
        dut.expect_exact("KEEP_REMOUNT ok=1", timeout=20)
        dut.expect_exact("KEEP_REREAD ok=1 data=MSC_FAT_PEER")
        dut.expect_exact("KEEP_FINAL_UNMOUNT ok=1")

    run_checks([
        peer_volume_is_formatted,
        mount_read_unmount,
        unmount_then_end_then_rebegin,
        end_releases_a_still_mounted_volume,
    ])
