def test_usb_serial_device_to_host(dut, peers):
    device = peers["device"]

    device.write("d")
    dut.expect_exact("SERIAL_RX device to host")


def test_usb_serial_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")
