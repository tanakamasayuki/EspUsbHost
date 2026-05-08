def test_host_logic(dut):
    dut.expect_exact("TEST_BEGIN")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
