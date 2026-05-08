def test_hid_vendor(dut, peers):
    device = peers["device"]

    device.write("h")
    dut.expect_exact("VENDOR hello vendor")
