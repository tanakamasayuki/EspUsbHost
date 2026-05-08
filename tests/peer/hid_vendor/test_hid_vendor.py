def test_hid_vendor(dut, peers):
    device = peers["device"]

    device.write("h")
    dut.expect_exact("VENDOR hello vendor")

    dut.write("f")
    dut.expect_exact("SEND_FEATURE 1")

    dut.write("o")
    dut.expect_exact("SEND_OUTPUT 1")
