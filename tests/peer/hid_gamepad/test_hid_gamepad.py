def test_hid_gamepad_axes(dut, peers):
    device = peers["device"]

    device.write("a")
    dut.expect_exact("GAMEPAD x=10 y=-10 z=20 rz=-20 rx=30 ry=-30 hat=3 buttons=0x00000005 previous=0x00000000")

    device.write("0")
    dut.expect_exact("GAMEPAD x=0 y=0 z=0 rz=0 rx=0 ry=0 hat=0 buttons=0x00000000 previous=0x00000005")


def test_hid_gamepad_hat(dut, peers):
    device = peers["device"]

    for cmd, hat in [("1", 1), ("2", 2), ("3", 3), ("4", 4), ("5", 5), ("6", 6), ("7", 7), ("8", 8)]:
        device.write(cmd)
        dut.expect_exact(f"GAMEPAD x=0 y=0 z=0 rz=0 rx=0 ry=0 hat={hat} buttons=0x00000000 previous=0x00000000")
        device.write("0")
        dut.expect_exact("GAMEPAD x=0 y=0 z=0 rz=0 rx=0 ry=0 hat=0 buttons=0x00000000 previous=0x00000000")


def test_hid_gamepad_buttons(dut, peers):
    device = peers["device"]

    device.write("b")
    dut.expect_exact("GAMEPAD x=0 y=0 z=0 rz=0 rx=0 ry=0 hat=0 buttons=0x00007fff previous=0x00000000")

    device.write("0")
    dut.expect_exact("GAMEPAD x=0 y=0 z=0 rz=0 rx=0 ry=0 hat=0 buttons=0x00000000 previous=0x00007fff")
