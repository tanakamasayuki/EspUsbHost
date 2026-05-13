def test_loopback_hid_keyboard(dut):
    dut.expect_exact("TEST_BEGIN loopback_hid_keyboard")
    dut.expect_exact("HOST_READY fs")
    dut.expect_exact("USB_DEVICE_READY")
    dut.expect_exact("HOST_DEVICE")
    dut.expect_exact("KEY a")
    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
