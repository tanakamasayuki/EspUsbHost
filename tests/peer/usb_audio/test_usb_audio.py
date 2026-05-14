def test_usb_audio_device_to_host(dut, peers):
    device = peers["device"]

    device.expect_exact("AUDIO_DEVICE_READY")
    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("a")
    device.expect("DEVICE_TX_AUDIO [1-9][0-9]*")
    dut.expect("AUDIO_RX addr=[0-9]+ iface=[0-9]+ total=[1-9][0-9]* last=[1-9][0-9]*")
