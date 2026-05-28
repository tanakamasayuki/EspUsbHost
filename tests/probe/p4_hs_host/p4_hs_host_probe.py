def test_p4_hs_host_probe(dut):
    dut.expect_exact("TEST_BEGIN p4_hs_host_probe")
    dut.expect_exact("HOST_READY hs")
    dut.expect_exact("HOST_DEVICE connected", timeout=30)
