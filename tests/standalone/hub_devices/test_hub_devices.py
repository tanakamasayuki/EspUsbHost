def test_hub_inventory(dut):
    dut.expect_exact("READY hub_devices")
    dut.write("i")
    dut.expect_exact("TEST_BEGIN hub_inventory")
    dut.expect_exact("INVENTORY_DONE")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"


def test_ch340_loopback_result(dut):
    # The device is optional for this standalone suite. If connected, it must pass.
    dut.write("c")
    dut.expect_exact("TEST_BEGIN ch340_loopback")
    result = dut.expect_exact(["PASS ch340_loopback", "SKIP ch340_loopback not_connected"])
    assert result in (b"PASS ch340_loopback", b"SKIP ch340_loopback not_connected")
    dut.expect_exact("TEST_END")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
