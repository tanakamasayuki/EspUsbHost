"""UAC2 peer test.

The Arduino core's USBAudioCard is UAC1 only, so this test drives a peer built
with the sibling EspUsbDevice library and ``EspUsbAudioProtocol::Uac2``. It covers
the parts of the host that UAC1 never exercises:

* the class revision taken from bInterfaceProtocol / bcdADC
* AS_GENERAL as the source of the channel count (UAC2 Format Type I has none)
* the Clock Source entity behind bTerminalLink, and the sample rates read from it
  with a SAM_FREQ RANGE request instead of from the descriptors
* the 4-byte / 2-bit Feature Unit bmaControls layout and the volume RANGE request
* the explicit feedback IN endpoint of the asynchronous playback interface, which
  must not be mistaken for a capture stream

It also covers the format-selection API that is not UAC2-specific: starting with
zeroed format arguments, which resolves the best format the device offers.
"""


import time

import pexpect


def _discard_previous_output(dut):
    """Drop serial output that was buffered before this test body started.

    The DUT is flashed before the peer, so by the time this test begins it has
    already booted and enumerated the peer's *previous* firmware - which is this
    same UAC2 audio device. That output is still queued here, and because the test
    drives the DUT with single-character commands, matching a stale line would send
    the next command before the device of this run is enumerated and desynchronise
    everything after it.

    Each greedy match consumes whatever has been read so far; repeat until a match
    times out, which means nothing is left. This cannot swallow the output of this
    run: the peer only starts its USB audio function seconds later, after its own
    reset delay.
    """
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            dut.expect(r"[\s\S]+", timeout=0.5)
        except pexpect.TIMEOUT:
            return
    raise AssertionError("DUT keeps producing output; cannot synchronise")


def test_usb_audio_uac2_bidirectional(dut, peers):
    device = peers["device"]
    _discard_previous_output(dut)

    device.expect_exact("AUDIO_DEVICE_READY")
    dut.expect("AUDIO_IN_READY addr=[0-9]+")
    dut.expect("AUDIO_OUT_READY addr=[0-9]+")

    # Feature Units are parsed with the UAC2 layout: protocol 0x20 and a 4-byte
    # bmaControls stride. Mute and volume are decoded from 2-bit fields.
    dut.expect(r"AUDIO_UNIT unit=[0-9]+ source=[0-9]+ channels=1 control_size=4 "
               r"master=0xf proto=0x20 mute=1 volume=1")

    # UAC2 sample rates come from the clock entity, so ask for the stream dump
    # after enumeration rather than reading it out of the connect callback.
    dut.write("d")
    dut.expect(r"AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x0[0-9a-f] dir=OUT channels=1 bytes=2 "
               r"bits=16 rate=48000 rates=1 first=48000 min=48000 max=48000 maxPacket=[0-9]+ "
               r"interval=1 proto=0x20 terminal=[1-9][0-9]* clock=[1-9][0-9]* startable=1")
    dut.expect(r"AUDIO_STREAM iface=[0-9]+ alt=1 ep=0x8[0-9a-f] dir=IN channels=1 bytes=2 "
               r"bits=16 rate=48000 rates=1 first=48000 min=48000 max=48000 maxPacket=[0-9]+ "
               r"interval=1 proto=0x20 terminal=[1-9][0-9]* clock=[1-9][0-9]* startable=1")
    # Exactly one OUT and one IN stream: the playback interface's feedback IN
    # endpoint must not have been registered as a third stream.
    dut.expect_exact("AUDIO_STREAM_COUNT 2")

    # UAC2 reports MIN/MAX/RES in a single RANGE response. EspUsbDevice's volume
    # control spans -90 dB to 0 dB in 1 dB steps (1/256 dB units).
    dut.write("v")
    dut.expect_exact("AUDIO_VOLUME_RANGE min=-23040 max=0 res=256")

    dut.write("w")
    dut.expect_exact("AUDIO_VOLUME set=1 get=1 db=-6.00")

    dut.write("M")
    dut.expect_exact("AUDIO_MUTE set=1 get=1 muted=1 clear=1 get2=1 muted2=0")

    # Zero arguments mean "no preference": the library resolves the best format the
    # device offers, which here is its only one.
    dut.write("A")
    dut.expect_exact("AUDIO_OUT_AUTO started=1 channels=1 bits=16 rate=48000")
    device.expect_exact("AUDIO_INTERFACE SPK 1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("r")
    device.expect_exact("DEVICE_AUDIO_RESET")

    dut.write("s")
    dut.expect("AUDIO_TX [1-9][0-9]*")
    device.expect("DEVICE_RX_AUDIO [1-9][0-9]*")

    # The peer's playback interface is asynchronous, so it also has an explicit
    # feedback IN endpoint. Now that playback is running, the host must be polling
    # it and pacing the OUT packets from the rate it reports.
    time.sleep(0.5)
    dut.write("f")
    feedback = dut.expect(
        r"AUDIO_FEEDBACK has=1 rate=([0-9]+) updates=([1-9][0-9]*) rejects=([0-9]+) pacing=([0-9]+)")
    rate = int(feedback.group(1))
    updates = int(feedback.group(2))
    rejects = int(feedback.group(3))
    pacing = int(feedback.group(4))
    # The host ignores anything outside +/-12.5% of 48000, so assert a tighter
    # window: a device tracking a 48 kHz clock has no reason to ask for more than
    # a few percent of correction.
    assert 45600 <= rate <= 50400, f"feedback rate {rate} is not near 48000"
    assert pacing == rate, f"pacing rate {pacing} does not follow feedback rate {rate}"
    # The peer computes its feedback from its FIFO level, so the packets it sends
    # before the FIFO is primed are out of range and get ignored. Those are the
    # only rejects expected; they must stay a small minority.
    assert rejects * 10 < updates, f"{rejects} rejected feedback packets out of {updates}"

    # The feedback endpoint keeps reporting, not just once at start.
    time.sleep(0.5)
    dut.write("f")
    again = dut.expect(
        r"AUDIO_FEEDBACK has=1 rate=[0-9]+ updates=([0-9]+) rejects=([0-9]+) pacing=[0-9]+")
    assert int(again.group(1)) > updates, "feedback updates stopped after the first packet"
    assert int(again.group(2)) * 10 < int(again.group(1))

    dut.write("I")
    dut.expect_exact("AUDIO_IN_AUTO started=1 channels=1 bits=16 rate=48000")
    device.expect_exact("AUDIO_INTERFACE MIC 1")

    dut.write("r")
    dut.expect_exact("AUDIO_RESET")

    device.write("m")
    device.expect("DEVICE_TX_AUDIO [1-9][0-9]*")
    dut.expect("AUDIO_RX addr=[0-9]+ iface=[0-9]+ total=[1-9][0-9]* last=[1-9][0-9]*")

    # The fully specified form still resolves the same streams (both are already
    # running, so these only exercise the exact-match lookup).
    dut.write("a")
    dut.expect_exact("AUDIO_OUT_START 1")
    dut.write("i")
    dut.expect_exact("AUDIO_IN_START 1")
