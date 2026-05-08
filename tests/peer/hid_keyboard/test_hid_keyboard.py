def test_host_logic(dut, peers):
    keyboard = peers["keyboard"]
    dut.expect_exact("hello, keyboard.")
