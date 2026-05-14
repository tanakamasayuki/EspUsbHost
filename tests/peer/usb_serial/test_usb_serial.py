def test_usb_serial_device_to_host(dut, peers):
    device = peers["device"]

    device.write("d")
    dut.expect_exact("SERIAL_RX device to host")


def test_usb_serial_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")


def test_usb_serial_dtr_rts(dut, peers):
    device = peers["device"]

    dut.write("d")
    dut.expect_exact("SERIAL_DTR 1")
    device.expect_exact("LINE_STATE dtr=1 rts=0")

    dut.write("r")
    dut.expect_exact("SERIAL_RTS 1")
    device.expect_exact("LINE_STATE dtr=1 rts=1")

    dut.write("D")
    dut.expect_exact("SERIAL_DTR_OFF 1")
    device.expect_exact("LINE_STATE dtr=0 rts=1")

    dut.write("R")
    dut.expect_exact("SERIAL_RTS_OFF 1")
    device.expect_exact("LINE_STATE dtr=0 rts=0")
