def test_usb_audio_device_to_host(dut, peers):
    device = peers["device"]

    device.expect_exact("AUDIO_DEVICE_READY")
    dut.expect("AUDIO_READY addr=[0-9]+")
    dut.expect("AUDIO_OUT_READY addr=[0-9]+")
    device.expect_exact("AUDIO_INTERFACE SPK 1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")

    dut.write("s")
    dut.expect("AUDIO_TX [1-9][0-9]*")
    device.expect("DEVICE_RX_AUDIO [1-9][0-9]*")
