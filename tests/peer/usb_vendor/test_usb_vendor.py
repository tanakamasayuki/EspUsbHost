import time


def _wait_vendor_ready(dut, device, timeout=15):
    """Wait for the final peer identity and its descriptors to be available."""
    device.write("?")
    device.expect_exact("DEVICE_READY")

    deadline = time.monotonic() + timeout
    while True:
        dut.write("i")
        match = dut.expect(
            r"VENDOR_ENUM interface=(\d) bulk_out=(\d) bulk_in=(\d) "
            r"interfaces=(\d+) endpoints=(\d+)",
            timeout=min(2, max(0.1, deadline - time.monotonic())),
        )
        if match.group(1) == b"1" and match.group(2) == b"1" and match.group(3) == b"1":
            return
        if time.monotonic() >= deadline:
            raise AssertionError(f"USB Vendor device did not become ready within {timeout}s")
        time.sleep(0.1)


def test_usb_vendor_enumeration(dut, peers):
    device = peers["device"]
    _wait_vendor_ready(dut, device)

    # Query once more to verify the public descriptor details independently of
    # the readiness polling above.
    dut.write("i")
    dut.expect_exact("INTERFACE number=0 class=0xff subclass=0x00 protocol=0x00 endpoints=2")
    dut.expect_exact("VENDOR_ENUM interface=1 bulk_out=1 bulk_in=1")


def test_usb_vendor_bulk_and_control(dut, peers):
    device = peers["device"]
    _wait_vendor_ready(dut, device)

    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")
    dut.write("w")
    dut.expect_exact("VENDOR_WRITE 1")
    dut.write("p")
    dut.expect_exact("VENDOR_DATA seen=1 data=echo:ping")
    dut.write("r")
    dut.expect_exact("VENDOR_READ len=9 data=echo:ping")
    dut.write("c")
    dut.expect_exact("VENDOR_CONTROL in=1 len=18 data=EspUsbDeviceVendor out=1")
    dut.write("u")
    dut.expect("WEBUSB_URL ok=1 len=[1-9][0-9]* found=1")


def test_usb_vendor_on_demand_read(dut, peers):
    """vendorOpen() by interface number with on-demand reads, plus vendorReadSync().

    A transactional bulk protocol (Bulk-Only Transport, for one) answers only
    inside a transaction, so a continuously outstanding IN transfer is a transfer
    error the rest of the time. This checks the other mode: nothing is submitted
    on the IN endpoint until vendorReadSync() asks, and the answer comes back on
    that one transfer.
    """
    device = peers["device"]
    _wait_vendor_ready(dut, device)

    # The read mode is fixed when the interface is opened, so an earlier test
    # that opened it in continuous mode has to be undone first.
    dut.write("x")
    dut.expect_exact("HOST_END installed=0 clients=-1 devices=-1 ready=0")
    dut.expect_exact("HOST_REBEGIN 1")
    _wait_vendor_ready(dut, device)

    dut.write("O")
    dut.expect_exact("VENDOR_OPEN_ONDEMAND 1")
    dut.write("R")
    dut.expect_exact("VENDOR_READ_SYNC write=1 ok=1 len=9 data=echo:ping")

    # Leave the interface closed again: the read mode is fixed at open time, so a
    # later test opening it in continuous mode would otherwise be refused.
    dut.write("x")
    dut.expect_exact("HOST_END installed=0 clients=-1 devices=-1 ready=0")
    dut.expect_exact("HOST_REBEGIN 1")


def test_usb_vendor_end_rebegin_with_device_open(dut, peers):
    """end() fully uninstalls an active host and the same object can restart."""
    device = peers["device"]
    for _ in range(2):
        _wait_vendor_ready(dut, device)
        dut.write("o")
        dut.expect_exact("VENDOR_OPEN 1")
        dut.write("x")
        dut.expect_exact("HOST_END installed=0 clients=-1 devices=-1 ready=0")
        dut.expect_exact("HOST_REBEGIN 1")

    _wait_vendor_ready(dut, device)
    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")
    dut.write("w")
    dut.expect_exact("VENDOR_WRITE 1")
    dut.write("p")
    dut.expect_exact("VENDOR_DATA seen=1 data=echo:ping")
