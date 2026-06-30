def test_usb_vendor_enumeration(dut, peers):
    dut.expect_exact("HOST_CONNECTED")
    dut.write("i")
    dut.expect_exact("INTERFACE number=0 class=0xff subclass=0x00 protocol=0x00 endpoints=2")
    dut.expect_exact("VENDOR_ENUM interface=1 bulk_out=1 bulk_in=1")


def test_usb_vendor_bulk_and_control(dut, peers):
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
