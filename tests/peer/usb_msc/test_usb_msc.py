"""USB Mass Storage against a peer presenting a 16-block FAT12 volume.

One test for the module, with the SCSI operations as named checks inside it.
They are checks rather than tests because none of them needs anything attached
per case -- no marker, no selection, no per-node-id machinery -- and a test each
would pay pytest's per-test cost twenty times for nothing. A failing check still
names itself: it is an ordinary frame in the traceback.
"""

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
    runs when a test fails or is interrupted too, so the board is not left
    hosting USB after the run.
    """
    _poll_state(dut, r"HOST_STATE (?:idle|running) devices=\d+")
    dut.write("G")
    yield
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")


def test_usb_msc(dut, peers, run_checks):
    device = peers["device"]

    def capacity():
        dut.write("c")
        dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")

    def block_device_info():
        dut.write("d")
        dut.expect(r"MSC_BLOCK_DEVICE ok=1 addr=\d+ iface=\d+ lun=0 max_lun=0 "
                   r"blocks=16 block_size=512 bytes=8192\r?\n")

    def capacity64():
        dut.write("C")
        dut.expect_exact("MSC_CAPACITY64 ok=1 blocks=16 block_size=512")

    def inquiry():
        dut.write("i")
        dut.expect_exact(
            "MSC_INQUIRY ok=1 removable=1 vendor='ESP32' product='MSC_PEER' revision='1.0'")

    def max_lun():
        dut.write("l")
        dut.expect_exact("MSC_MAX_LUN ok=1 max_lun=0")

    def select_lun():
        dut.write("L")
        dut.expect_exact("MSC_SELECT_LUN ok=1")

    def request_sense():
        # Another check in this module deliberately provokes an error, and REQUEST
        # SENSE reports whatever is pending -- so asserting a clean sense only
        # holds if this check establishes it. Drain the pending sense and issue a
        # command known to succeed, so the clean sense below is one this check
        # produced rather than one it inherited.
        dut.write("s")
        # Through the newline: expect() returns as soon as the buffer allows a
        # match, and [^\r\n]* is satisfied by zero characters, so without it this
        # can consume "MSC_SENSE " and leave the rest of the line behind.
        dut.expect(r"MSC_SENSE [^\r\n]*\r?\n")
        dut.write("t")
        dut.expect_exact("MSC_TEST_UNIT_READY ok=1")

        dut.write("s")
        dut.expect_exact("MSC_SENSE ok=1 response=0x70 key=0x00 asc=0x00 ascq=0x00")
        dut.write("S")
        dut.expect_exact("MSC_LAST_SENSE ok=1 response=0x70 key=0x00 asc=0x00 ascq=0x00")

    def test_unit_ready():
        dut.write("t")
        dut.expect_exact("MSC_TEST_UNIT_READY ok=1")

    def wait_ready():
        dut.write("T")
        dut.expect_exact("MSC_WAIT_READY ok=1")

    def synchronize_cache():
        dut.write("y")
        dut.expect_exact("MSC_SYNC_CACHE ok=1")

    def read_boot_block():
        dut.write("r")
        dut.expect_exact("MSC_READ ok=1 b0=eb b1=3c b510=55 b511=aa")

    def read64_boot_block():
        dut.write("R")
        dut.expect_exact("MSC_READ64 ok=1 b0=eb b1=3c b510=55 b511=aa")

    def write_read_block():
        dut.write("w")
        device.expect_exact("DEVICE_WRITE lba=4 offset=0 size=512")
        dut.expect_exact("MSC_WRITE_READ write=1 read=1 b0=a5 b1=a4 b255=5a b511=5a")

    def write_read64_block():
        dut.write("W")
        device.expect_exact("DEVICE_WRITE lba=5 offset=0 size=512")
        dut.expect_exact("MSC_WRITE_READ64 write=1 read=1 b0=5a b1=5b b255=a5 b511=a5")

    def multi_block_write_read():
        dut.write("m")
        device.expect_exact("DEVICE_WRITE lba=6 offset=0 size=512")
        device.expect_exact("DEVICE_WRITE lba=7 offset=0 size=512")
        dut.expect_exact("MSC_MULTI write=1 read=1 b0=31 b511=30 b512=31 b1023=30")

    def chunked_write_read():
        dut.write("g")
        dut.expect_exact("MSC_CHUNKED write=1 read=1 b0=17 b4095=14 b4096=17 b4607=14")

    def out_of_range_is_rejected():
        dut.write("o")
        dut.expect_exact("MSC_OUT_OF_RANGE read=0 write=0")

    def failed_write_is_reported():
        device.write("F")
        device.expect_exact("DEVICE_FAIL_NEXT_WRITE armed=1")
        dut.write("e")
        device.expect_exact("DEVICE_WRITE_FAIL lba=10 offset=0 size=512")
        dut.expect_exact("MSC_FAILED_WRITE write=0")

        # Reset recovery must leave the bulk endpoints usable for the next command.
        dut.write("w")
        device.expect_exact("DEVICE_WRITE lba=4 offset=0 size=512")
        dut.expect_exact("MSC_WRITE_READ write=1 read=1 b0=a5 b1=a4 b255=5a b511=5a")

    def end_rebegin_with_device_attached():
        """end() uninstalls the host library while the MSC device is attached."""
        dut.write("X")
        dut.expect_exact("HOST_STOP installed=0 clients=-1 devices=-1 ready=0")
        dut.expect_exact("HOST_RESTART ready=1 error=ESP_OK")

        dut.write("c")
        dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")

    def end_rebegin_with_empty_device_list():
        """end() must uninstall the host library with nothing left on the bus.

        Reported as issue #42: begin() after end() fails with ESP_ERR_INVALID_STATE.
        usb_host_device_free_all() returns ESP_OK for an empty device list, so the
        shutdown handles no library event and the USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS
        flag left behind by the client deregister makes usb_host_uninstall() refuse.
        The host library stays installed, so every later begin() fails in
        usb_host_install(). A device attached during teardown hides this, because
        then the ALL_FREE wait handles the events that clear the flag.

        This check reboots the peer, so it leaves the bus as it found it rather
        than merely ending with it working: it waits for the device to come back
        before returning, which is what lets it run in any position.
        """
        dut.write("x")
        dut.expect_exact("HOST_RESTART_ARMED 1")

        # Rebooting the peer is the only way this setup can empty the bus; the
        # device core has no USB detach API. The DUT runs the cycle on its own as
        # soon as the disconnect arrives, so the ~500 ms the peer needs to come
        # back is not spent waiting for a serial round trip.
        device.write("z")
        device.expect_exact("DEVICE_REBOOT")

        dut.expect_exact("DEVICE_DISCONNECTED", timeout=20)
        dut.expect_exact("HOST_IDLE_STOP devices_before=0 installed=0 error=ESP_OK")
        dut.expect_exact("HOST_IDLE_RESTART ready=1 error=ESP_OK")

        # The peer must enumerate again on the restarted host and still work.
        dut.expect_exact("DEVICE_CONNECTED", timeout=30)
        dut.write("c")
        dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")

    run_checks([
        capacity,
        block_device_info,
        capacity64,
        inquiry,
        max_lun,
        select_lun,
        request_sense,
        test_unit_ready,
        wait_ready,
        synchronize_cache,
        read_boot_block,
        read64_boot_block,
        write_read_block,
        write_read64_block,
        multi_block_write_read,
        chunked_write_read,
        out_of_range_is_rejected,
        failed_write_is_reported,
        end_rebegin_with_device_attached,
        end_rebegin_with_empty_device_list,
    ])
