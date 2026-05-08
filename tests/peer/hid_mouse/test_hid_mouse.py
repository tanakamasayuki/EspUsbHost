def test_hid_mouse_move_and_click(dut, peers):
    device = peers["device"]

    device.write("r")
    dut.expect_exact("MOUSE x=40 y=0 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("d")
    dut.expect_exact("MOUSE x=0 y=40 wheel=0 buttons=0 previous=0 moved=1 changed=0")

    device.write("w")
    dut.expect_exact("MOUSE x=0 y=0 wheel=1 buttons=0 previous=0 moved=1 changed=0")

    device.write("m")
    dut.expect_exact("buttons=1 previous=0 moved=0 changed=1")
    dut.expect_exact("buttons=0 previous=1 moved=0 changed=1")
