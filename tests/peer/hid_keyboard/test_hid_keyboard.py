def test_host_logic(dut, peers):
    str = "hello, keyboard"
    keyboard = peers["keyboard"]
    keyboard.write(str)
    dut.expect_exact(str)
