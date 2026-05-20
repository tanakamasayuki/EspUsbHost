def test_custom_hid_input(dut, peers):
    device = peers["device"]

    dut.expect_exact("HID_REPORT iface=0 reported=38 len=38 first=05 last=c0")

    device.write("a")
    dut.expect_exact("CUSTOM len=8 data=017f812233445566")
