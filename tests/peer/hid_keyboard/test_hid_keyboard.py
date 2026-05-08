def test_host_logic(dut, peers):
    str = "hello, keyboard"
    device = peers["device"]
    device.write(str)
    dut.expect_exact(str)
