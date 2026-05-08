def test_hid_gamepad(dut, peers):
    device = peers["device"]

    device.write("a")
    dut.expect_exact("GAMEPAD x=10 y=-10 z=20 rz=-20 rx=30 ry=-30 hat=3 buttons=0x00000005 previous=0x00000000")

    device.write("0")
    dut.expect_exact("GAMEPAD x=0 y=0 z=0 rz=0 rx=0 ry=0 hat=0 buttons=0x00000000 previous=0x00000005")
