def test_hid_consumer_control(dut, peers):
    device = peers["device"]

    device.write("u")
    dut.expect_exact("CONSUMER usage=0x00e9 pressed=1 released=0")
    dut.expect_exact("CONSUMER usage=0x00e9 pressed=0 released=1")

    device.write("d")
    dut.expect_exact("CONSUMER usage=0x00ea pressed=1 released=0")
    dut.expect_exact("CONSUMER usage=0x00ea pressed=0 released=1")
