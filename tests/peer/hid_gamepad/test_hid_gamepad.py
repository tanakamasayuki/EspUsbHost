def test_hid_gamepad_axes(dut, peers):
    device = peers["device"]

    device.write("a")
    dut.expect("GAMEPAD report=0a f6 14 ec 1e e2 03 05 00 00 00 fields=[1-9][0-9]*")

    device.write("0")
    dut.expect("GAMEPAD report=00 00 00 00 00 00 00 00 00 00 00 fields=[1-9][0-9]*")


def test_hid_gamepad_hat(dut, peers):
    device = peers["device"]

    for cmd, hat in [("1", 1), ("2", 2), ("3", 3), ("4", 4), ("5", 5), ("6", 6), ("7", 7), ("8", 8)]:
        device.write(cmd)
        dut.expect(f"GAMEPAD report=00 00 00 00 00 00 {hat:02x} 00 00 00 00 fields=[1-9][0-9]*")
        device.write("0")
        dut.expect("GAMEPAD report=00 00 00 00 00 00 00 00 00 00 00 fields=[1-9][0-9]*")


def test_hid_gamepad_buttons(dut, peers):
    device = peers["device"]

    device.write("b")
    dut.expect("GAMEPAD report=00 00 00 00 00 00 00 ff 7f 00 00 fields=[1-9][0-9]*")

    device.write("0")
    dut.expect("GAMEPAD report=00 00 00 00 00 00 00 00 00 00 00 fields=[1-9][0-9]*")
