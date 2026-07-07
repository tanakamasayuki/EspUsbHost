def test_usb_audio_bidirectional(dut, peers):
    device = peers["device"]

    device.expect_exact("AUDIO_DEVICE_READY")
    dut.expect("AUDIO_IN_READY addr=[0-9]+")
    dut.expect("AUDIO_OUT_READY addr=[0-9]+")
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x01 dir=OUT channels=1 bytes=2 bits=16 rate=48000 rates=1 first=48000 min=0 max=0 maxPacket=98 interval=1")
    dut.expect("AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 first=48000 min=0 max=0 maxPacket=98 interval=1")

    dut.write("a")
    dut.expect_exact("AUDIO_OUT_START 1")
    device.expect_exact("AUDIO_INTERFACE SPK 1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")

    dut.write("s")
    dut.expect("AUDIO_TX [1-9][0-9]*")
    device.expect("DEVICE_RX_AUDIO [1-9][0-9]*")

    dut.write("i")
    dut.expect_exact("AUDIO_IN_START 1")
    device.expect_exact("AUDIO_INTERFACE MIC 1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("m")
    device.expect("DEVICE_TX_AUDIO [1-9][0-9]*")
    dut.expect("AUDIO_RX addr=[0-9]+ iface=[0-9]+ total=[1-9][0-9]* last=[1-9][0-9]*")
