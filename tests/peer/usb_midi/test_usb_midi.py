def test_usb_midi_device_to_host(dut, peers):
    device = peers["device"]

    device.write("n")
    device.expect_exact("DEVICE_TX_NOTE_ON")
    dut.expect_exact("MIDI_RX cable=0 cin=09 status=90 data1=64 data2=110")


def test_usb_midi_port_info(dut, peers):
    # The cable configuration comes from the descriptors, so it is available
    # without any traffic having been exchanged. The peer's USBMIDI interface is
    # the single-cable default: one embedded jack on each bulk endpoint.
    #
    # A multi-cable peer needs EspUsbDevice to emit more than one embedded jack;
    # until then this covers the one-cable case only.
    assert peers  # the peer must be attached for the host to have a MIDI device

    dut.write("i")
    dut.expect_exact("MIDI_PORT_INFO ok=1 in=1 out=1")


def test_usb_midi_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("n")
    dut.expect_exact("MIDI_TX_NOTE_ON 1")
    device.expect_exact("DEVICE_RX cin=09 status=90 data1=60 data2=100")


def test_usb_midi_channel_messages_device_to_host(dut, peers):
    device = peers["device"]

    device.write("p")
    device.expect_exact("DEVICE_TX_PROGRAM")
    dut.expect_exact("MIDI_RX cable=0 cin=0c status=c0 data1=10 data2=0")

    device.write("b")
    device.expect_exact("DEVICE_TX_BEND")
    dut.expect_exact("MIDI_RX cable=0 cin=0e status=e0 data1=0 data2=72")

    device.write("a")
    device.expect_exact("DEVICE_TX_PRESSURE")
    dut.expect_exact("MIDI_RX cable=0 cin=0d status=d0 data1=77 data2=0")

    device.write("y")
    device.expect_exact("DEVICE_TX_POLY_PRESSURE")
    dut.expect_exact("MIDI_RX cable=0 cin=0a status=a0 data1=60 data2=80")

    device.write("c")
    device.expect_exact("DEVICE_TX_CC")
    dut.expect_exact("MIDI_RX cable=0 cin=0b status=b0 data1=74 data2=64")


def test_usb_midi_channel_messages_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("p")
    dut.expect_exact("MIDI_TX_PROGRAM 1")
    device.expect_exact("DEVICE_RX cin=0c status=c0 data1=10 data2=0")

    dut.write("b")
    dut.expect_exact("MIDI_TX_BEND 1")
    device.expect_exact("DEVICE_RX cin=0e status=e0 data1=0 data2=72")

    dut.write("a")
    dut.expect_exact("MIDI_TX_PRESSURE 1")
    device.expect_exact("DEVICE_RX cin=0d status=d0 data1=77 data2=0")

    dut.write("y")
    dut.expect_exact("MIDI_TX_POLY_PRESSURE 1")
    device.expect_exact("DEVICE_RX cin=0a status=a0 data1=60 data2=80")

    dut.write("c")
    dut.expect_exact("MIDI_TX_CC 1")
    device.expect_exact("DEVICE_RX cin=0b status=b0 data1=74 data2=64")


def test_usb_midi_sysex_host_to_device(dut, peers):
    device = peers["device"]

    dut.write("s")
    dut.expect_exact("MIDI_TX_SYSEX 1")
    device.expect_exact("DEVICE_RX cin=04 status=f0 data1=125 data2=1")
    device.expect_exact("DEVICE_RX cin=06 status=02 data1=247 data2=0")


def test_usb_midi_message_listeners(dut, peers):
    device = peers["device"]

    dut.write("l")
    dut.expect_exact("MIDI_LISTENER_SETUP empty=1 ids=1 capacity=1 invalid_remove=1 max=4")

    # The callback and the listeners share one snapshot taken before dispatch.
    # Listener 1 removes itself and adds listener 5, which must not run for this
    # message even though it is registered by the time the message is delivered.
    device.write("n")
    device.expect_exact("DEVICE_TX_NOTE_ON")
    dut.expect_exact("MIDI_RX cable=0 cin=09 status=90 data1=64 data2=110")
    dut.expect_exact("MIDI_LISTENER 1 status=90")
    dut.expect_exact("MIDI_LISTENER 2 status=90")
    dut.expect_exact("MIDI_LISTENER_STATE count=1")

    # The mutation takes effect on the next message, preserving registration
    # order: the existing listener 2 runs before the newly-added listener 5.
    device.write("p")
    device.expect_exact("DEVICE_TX_PROGRAM")
    dut.expect_exact("MIDI_RX cable=0 cin=0c status=c0 data1=10 data2=0")
    dut.expect_exact("MIDI_LISTENER 2 status=c0")
    dut.expect_exact("MIDI_LISTENER_STATE count=2")
    dut.expect_exact("MIDI_LISTENER 5 status=c0")

    dut.write("u")
    dut.expect_exact("MIDI_LISTENER_REMOVE removed=1 second=1")
    device.write("c")
    device.expect_exact("DEVICE_TX_CC")
    dut.expect_exact("MIDI_RX cable=0 cin=0b status=b0 data1=74 data2=64")
    dut.expect_exact("MIDI_LISTENER_STATE count=3")
    dut.expect_exact("MIDI_LISTENER 5 status=b0")

    # With the single callback dropped, listeners are still delivered: the
    # dispatch path must not be gated on the callback being present.
    dut.write("d")
    dut.expect_exact("MIDI_CALLBACK_DROP 1")
    device.write("b")
    device.expect_exact("DEVICE_TX_BEND")
    dut.expect_exact("MIDI_LISTENER_STATE count=4")
    dut.expect_exact("MIDI_LISTENER 5 status=e0")

    dut.write("e")
    dut.expect_exact("MIDI_CALLBACK_RESTORE 1")
    device.write("n")
    device.expect_exact("DEVICE_TX_NOTE_ON")
    dut.expect_exact("MIDI_RX cable=0 cin=09 status=90 data1=64 data2=110")

    dut.write("x")
    dut.expect_exact("MIDI_LISTENER_CLEAR 1")


def _expect_connect(dut, timeout=20):
    """Expect a connect event and check the listener saw what the callback saw."""
    connected = dut.expect(
        r"HOST_CONNECTED vid=([0-9a-f]{4}) pid=([0-9a-f]{4}) supported=([01])", timeout=timeout
    )
    listener = dut.expect(r"CONNECT_LISTENER 1 vid=([0-9a-f]{4}) supported=([01])")
    assert listener.group(1) == connected.group(1)
    assert listener.group(2) == connected.group(3)
    # A MIDI-only peer must report supported: the flag is built from HID / CDC /
    # audio / MSC / vendor-serial detection, and a MIDI streaming interface is
    # seen by none of them, so it needs its own term in that expression.
    assert connected.group(3) == b"1"


def test_usb_midi_lifecycle_listeners_on_reenumeration(dut, peers):
    """Connect listeners get the event that end() + begin() re-enumeration produces."""
    dut.write("k")
    # The lifecycle budget is deliberately larger than the per-input-event one.
    dut.expect_exact("LIFECYCLE_LISTENER_SETUP empty=1 ids=1 capacity=1 max=8")

    dut.write("r")
    dut.expect_exact("HOST_END 1")
    dut.expect_exact("HOST_REBEGIN 1")
    _expect_connect(dut)
    dut.expect_exact("CONNECT_LISTENER 2")

    dut.write("j")
    dut.expect_exact("CONNECT_LISTENER_REMOVE removed=1 second=1")
    dut.write("r")
    dut.expect_exact("HOST_END 1")
    dut.expect_exact("HOST_REBEGIN 1")
    _expect_connect(dut)
    # Listeners run synchronously in one dispatch, so a still-registered listener
    # 2 would print before this loop() reply. The reply arriving first is the
    # proof that removal took effect.
    dut.write("z")
    assert (
        dut.expect_exact(["CONNECT_LISTENER 2", "LIFECYCLE_LISTENER_CLEAR 1"])
        == b"LIFECYCLE_LISTENER_CLEAR 1"
    )


def test_usb_midi_lifecycle_listeners_on_peer_reboot(dut, peers):
    """A real disconnect and reconnect reach every lifecycle listener."""
    device = peers["device"]

    dut.write("k")
    dut.expect_exact("LIFECYCLE_LISTENER_SETUP empty=1 ids=1 capacity=1 max=8")

    # Rebooting the peer is the only way this setup can produce a genuine
    # disconnect; the device core has no USB detach API.
    device.write("z")
    device.expect_exact("DEVICE_REBOOT")

    dut.expect_exact("HOST_DISCONNECTED", timeout=20)
    # The event must still carry usable device information after the endpoints
    # have been released and while the listeners run.
    dut.expect(r"DISCONNECT_LISTENER 1 vid=[0-9a-f]{4} address=[1-9][0-9]*")
    dut.expect_exact("DISCONNECT_LISTENER 2")

    _expect_connect(dut, timeout=30)
    dut.expect_exact("CONNECT_LISTENER 2")

    dut.write("z")
    dut.expect_exact("LIFECYCLE_LISTENER_CLEAR 1")

    # The stack is usable again after the round trip.
    device.write("n")
    device.expect_exact("DEVICE_TX_NOTE_ON")
    dut.expect_exact("MIDI_RX cable=0 cin=09 status=90 data1=64 data2=110", timeout=20)
