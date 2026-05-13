def test_hid_keyboard(dut, peers):
    str = "hello, keyboard"
    device = peers["device"]
    device.write(str)
    dut.expect_exact(str)
