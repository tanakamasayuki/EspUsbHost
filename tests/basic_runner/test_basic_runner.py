def test_basic_runner_uses_library(dut):
    dut.expect_exact("ADD_RESULT 3")
