def test_usb_audio_device_to_host(dut, peers):
    device = peers["device"]

    device.expect_exact("AUDIO_DEVICE_READY")
    dut.expect("AUDIO_READY addr=[0-9]+")
    device.expect_exact("AUDIO_INTERFACE MIC 1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("a")
    device.expect("DEVICE_TX_AUDIO [1-9][0-9]*")
