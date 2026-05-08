def test_hid_system_control(dut, peers):
    device = peers["device"]

    device.write("p")
    dut.expect_exact("SYSTEM usage=0x01 pressed=1 released=0")
    dut.expect_exact("SYSTEM usage=0x01 pressed=0 released=1")

    device.write("s")
    dut.expect_exact("SYSTEM usage=0x02 pressed=1 released=0")
    dut.expect_exact("SYSTEM usage=0x02 pressed=0 released=1")
