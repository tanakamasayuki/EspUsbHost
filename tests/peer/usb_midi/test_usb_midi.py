def test_usb_midi_device_to_host(dut, peers):
    device = peers["device"]

    device.write("n")
    device.expect_exact("DEVICE_TX_NOTE_ON")
    dut.expect_exact("MIDI_RX cable=0 cin=09 status=90 data1=64 data2=110")


def test_usb_midi_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("n")
    dut.expect_exact("MIDI_TX_NOTE_ON 1")
    device.expect_exact("DEVICE_RX cin=09 status=90 data1=60 data2=100")
