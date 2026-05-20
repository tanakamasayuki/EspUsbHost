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
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=57600 stop=2 parity=2 data=7")
    dut.write("h")
    dut.expect_exact("SERIAL_TX 1")
    device.expect_exact("DEVICE_RX host to serial")

    dut.write("m")
    dut.expect_exact("SERIAL_CONFIG_MARK 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=300 stop=1 parity=3 data=5")

    dut.write("b")
    dut.expect_exact("SERIAL_BAUD 1")
    device.write("l")
    device.expect_exact("DEVICE_LINE_CODING seen=1 baud=115200 stop=1 parity=3 data=5")
