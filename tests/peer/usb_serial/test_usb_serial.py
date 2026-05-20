def test_usb_serial_device_to_host(dut, peers):
    device = peers["device"]

    device.write("d")
    dut.expect_exact("SERIAL_RX device to host")


def test_usb_serial_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")


def test_usb_serial_config_api(dut, peers):
    device = peers["device"]

    dut.write("c")
    dut.expect_exact("SERIAL_CONFIG 1")
    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")

    dut.write("b")
    dut.expect_exact("SERIAL_BAUD 1")
