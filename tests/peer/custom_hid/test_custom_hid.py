def test_custom_hid_input(dut, peers):
    device = peers["device"]

    device.write("a")
    dut.expect_exact("CUSTOM len=8 data=017f812233445566")
