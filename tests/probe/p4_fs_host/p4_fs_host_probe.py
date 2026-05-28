def test_p4_fs_host_probe(dut):
    dut.expect_exact("TEST_BEGIN p4_fs_host_probe")
    dut.expect_exact("HOST_READY fs")
    dut.expect_exact("HOST_DEVICE connected", timeout=30)
