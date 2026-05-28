def test_p4_hs_device_probe(dut):
    dut.expect_exact("TEST_BEGIN p4_hs_device_probe")
    dut.expect_exact("USB_DEVICE_READY hs")
    dut.expect_exact("USB_DEVICE_WAITING host_enumeration_must_be_checked_on_pc", timeout=10)
