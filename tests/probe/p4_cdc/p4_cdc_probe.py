def test_p4_cdc_probe(dut):
    dut.expect_exact("TEST_BEGIN p4_cdc_probe")
    dut.expect_exact("CDC_PROBE_READY")
    dut.expect_exact("CDC_PROBE_ALIVE", timeout=5)
