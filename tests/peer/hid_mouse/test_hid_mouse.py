def test_hid_mouse_move(dut, peers):
    device = peers["device"]

    device.write("r")
    dut.expect_exact("MOUSE x=40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("l")
    dut.expect_exact("MOUSE x=-40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("d")
    dut.expect_exact("MOUSE x=0 y=40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("u")
    dut.expect_exact("MOUSE x=0 y=-40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("w")
    dut.expect_exact("MOUSE x=0 y=0 wheel=1 buttons=0 previous=0 moved=1 changed=0")


def test_hid_mouse_buttons(dut, peers):
    device = peers["device"]

    device.write("m")
    dut.expect_exact("buttons=1 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=1 moved=0 changed=1")

    device.write("R")
    dut.expect_exact("buttons=2 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=2 moved=0 changed=1")

    device.write("M")
    dut.expect_exact("buttons=4 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=4 moved=0 changed=1")

    device.write("b")
    dut.expect_exact("buttons=8 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=8 moved=0 changed=1")

    device.write("f")
    dut.expect_exact("buttons=16 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=16 moved=0 changed=1")
