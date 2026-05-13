def test_hid_logic(dut):
    dut.expect_exact("TEST_BEGIN hid_logic")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
