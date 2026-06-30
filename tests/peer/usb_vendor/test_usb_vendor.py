def test_usb_vendor_enumeration(dut, peers):
    device = peers["device"]

    device.expect_exact("DEVICE_BEGIN 1")
    device.write("?")
    device.expect_exact("DEVICE_READY")
    dut.expect_exact("HOST_CONNECTED")
    dut.write("i")
    dut.expect_exact("INTERFACE number=0 class=0xff subclass=0x00 protocol=0x00 endpoints=2")
    dut.expect_exact("VENDOR_ENUM interface=1 bulk_out=1 bulk_in=1")
    device.write("s")
    device.expect_exact("DEVICE_STATUS rx=0 control=0")


def test_usb_vendor_bulk_and_control(dut, peers):
    device = peers["device"]

    dut.write("o")
    dut.expect_exact("VENDOR_OPEN 1")
    dut.write("w")
    dut.expect_exact("VENDOR_WRITE 1")
    dut.write("d")
    dut.expect_exact("VENDOR_DATA seen=1 data=echo:ping")
    dut.write("r")
    dut.expect_exact("VENDOR_READ len=9 data=echo:ping")
    dut.write("c")
    dut.expect_exact("VENDOR_CONTROL in=1 len=18 data=EspUsbDeviceVendor out=1")
    device.write("s")
    device.expect_exact("DEVICE_STATUS rx=4 control=2")
