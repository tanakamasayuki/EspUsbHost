def test_hid_consumer_control_volume(dut, peers):
    device = peers["device"]

    device.write("u")
    dut.expect_exact("CONSUMER usage=0x00e9 pressed=1 released=0")
    dut.expect_exact("CONSUMER usage=0x00e9 pressed=0 released=1")

    device.write("d")
    dut.expect_exact("CONSUMER usage=0x00ea pressed=1 released=0")
    dut.expect_exact("CONSUMER usage=0x00ea pressed=0 released=1")


def test_hid_consumer_control_playback(dut, peers):
    device = peers["device"]

    device.write("p")
    dut.expect_exact("CONSUMER usage=0x00cd pressed=1 released=0")
    dut.expect_exact("CONSUMER usage=0x00cd pressed=0 released=1")

    device.write("n")
    dut.expect_exact("CONSUMER usage=0x00b5 pressed=1 released=0")
    dut.expect_exact("CONSUMER usage=0x00b5 pressed=0 released=1")

    device.write("s")
    dut.expect_exact("CONSUMER usage=0x00b6 pressed=1 released=0")
    dut.expect_exact("CONSUMER usage=0x00b6 pressed=0 released=1")


def test_hid_consumer_control_mute(dut, peers):
    device = peers["device"]

    device.write("m")
    dut.expect_exact("CONSUMER usage=0x00e2 pressed=1 released=0")
    dut.expect_exact("CONSUMER usage=0x00e2 pressed=0 released=1")
