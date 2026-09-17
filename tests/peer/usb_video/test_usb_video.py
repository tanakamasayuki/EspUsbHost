"""USB Video (UVC) peer test.

Covers the whole path: discovering a camera during enumeration, reporting every
format/frame pair it advertises, negotiating Probe/Commit, selecting a streaming
alternate, and assembling the isochronous payloads back into video frames.

The frames the peer sends carry ``buffer[i] = i + n``, so the first byte names the
frame and every byte after it follows from it. Checking that pattern is what
separates a host that reassembled the frame from one that merely counted the
right number of bytes: a payload header left in the image data, a dropped
payload, or two frames joined together each break it, and none of them would show
up in a byte count.

The peer is built with the sibling EspUsbDevice library's ``EspUsbDeviceVideo``
and advertises exactly one MJPEG format at one size and one rate. That is not a
simplification for the test's sake -- it is the largest camera this host can see
at all on arduino-esp32 3.3.x, whose ``CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE``
is 256 bytes. A configuration descriptor larger than that is truncated before the
format descriptors are reached, so a real webcam reports zero streams. The first
test below asserts the peer stays under that limit, so the day it stops fitting
is reported as a descriptor-size failure rather than as a decoding failure.

Every assertion compares the host's decoded value against a number the peer
printed from its own descriptor builder, so a decoding mistake shows up as the
two boards disagreeing rather than as a mismatch with a constant copied here.
"""

import time

import pexpect
import pytest


def _poll_state(dut, pattern, command="Q", attempts=100):
    """Wait for a state the sketch answers on demand.

    Polling a query rather than waiting for a connect line matters because a
    connect line is printed once, when the device enumerates, so waiting for it
    only works while the test doing so happens to run first.
    """
    for _ in range(attempts):
        dut.write(command)
        try:
            dut.expect(pattern, timeout=2)
            return
        except Exception:
            continue
    raise AssertionError(f"the host never reported {pattern!r}")


@pytest.fixture(autouse=True)
def usb_host(dut, peers):
    """Start the USB host for this test, and stop it however the test ends.

    The sketch does not start it in setup(): the peer board is flashed after this
    board has booted, and a host that is already running observes those resets
    and records the enumerations they cause as errors.

    ``peers`` is requested for its ordering, not its value. This fixture is
    autouse and would otherwise be set up before it, which starts the host before
    the peer upload -- putting the host back inside exactly the window the gating
    exists to avoid.
    """
    _poll_state(dut, r"HOST_STATE (?:idle|running) devices=\d+")
    dut.write("G")
    yield
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")


def _discard_previous_output(dut):
    """Drop serial output buffered before this test body started.

    The DUT is flashed before the peer, so by the time a test begins it has
    already enumerated the peer's *previous* firmware -- the same camera. That
    output is still queued, and matching a stale line would send the next command
    before this run's device is enumerated and desynchronise everything after it.
    """
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            dut.expect(r"[\s\S]+", timeout=0.5)
        except pexpect.TIMEOUT:
            return
    raise AssertionError("DUT keeps producing output; cannot synchronise")


@pytest.fixture
def camera(dut, peers):
    """Enumerate the peer and return what both sides say about it.

    The peer answers ``v`` on demand rather than printing its facts once at boot,
    for the same reason the host answers ``Q``: a line printed at startup can be
    read by whichever test runs first and by no other.
    """
    device = peers["device"]
    _discard_previous_output(dut)
    _discard_previous_output(device)

    # Wait for the camera specifically, not merely for a device. The peer board
    # enumerates twice: an ESP32-S3 shows its ROM USB-Serial-JTAG unit on the same
    # connector until the sketch calls device.begin() seconds later, so a test that
    # waited on the device count would run against the wrong device.
    _poll_state(dut, r"VIDEO_DEVICE addr=[1-9]\d* streams=[1-9]\d*", command="v", attempts=60)

    device.write("v")
    match = device.expect(
        r"DEVICE_VIDEO format=MJPEG (\d+)x(\d+) fps=(\d+) max_frame=(\d+) packet=(\d+) bulk=(\d+)\r?\n")
    facts = {
        "width": int(match.group(1)),
        "height": int(match.group(2)),
        "fps": int(match.group(3)),
        "max_frame": int(match.group(4)),
        "packet": int(match.group(5)),
        "bulk": int(match.group(6)),
    }
    descriptor = device.expect(r"DEVICE_VIDEO_DESC len=(\d+)\r?\n")
    facts["descriptor_length"] = int(descriptor.group(1))
    frame = device.expect(r"DEVICE_VIDEO_FRAME bytes=(\d+)\r?\n")
    facts["frame_bytes"] = int(frame.group(1))

    return facts


def test_configuration_descriptor_fits_the_control_transfer_limit(camera):
    """The peer must stay inside the core's 256-byte control transfer limit.

    This is asserted before anything else because it is the precondition for
    every other test here: past it, the host never reads the format descriptors
    at all and reports zero streams. 9 bytes are the configuration descriptor
    header the camera's function descriptors follow.

    The headroom is small and worth stating in numbers, because the peer is the
    thing most likely to grow: a MJPEG frame descriptor is 38 bytes and a format
    descriptor 11 (27 uncompressed), so this peer fits two frame sizes at 198
    bytes and three at 236, and a fourth would not fit. A second format alongside
    the first lands around 225.
    """
    total = camera["descriptor_length"] + 9
    assert total <= 256, (
        f"the peer's configuration descriptor is {total} bytes, over the "
        f"CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE of 256 that arduino-esp32 "
        f"3.3.x builds the host with; the host cannot enumerate it. Drop a frame "
        f"size or a format from the peer rather than relaxing this")


def test_video_stream_is_discovered(dut, camera):
    """The host decodes the camera's one format/frame pair from its descriptors."""
    dut.write("d")
    stream = dut.expect(
        r"VIDEO_STREAM iface=(\d+) ep=0x([0-9a-f]{2}) (isoc|bulk) format=(\S+) (\d+)x(\d+) "
        r"formatIndex=(\d+) frameIndex=(\d+) fps=(\d+) rates=(\d+) min=(\d+) max=(\d+) "
        r"step=(\d+) max_frame=(\d+) payload=(\d+) startable=(\d+)\r?\n")
    fields = [f.decode() for f in stream.groups()]

    assert fields[3] == "MJPEG"
    assert fields[2] == ("bulk" if camera["bulk"] else "isoc")
    # The streaming endpoint is an IN endpoint: bit 7 of bEndpointAddress.
    assert int(fields[1], 16) & 0x80, "streaming endpoint is not an IN endpoint"

    # Size, rate and frame bound all come back as the peer built them.
    assert int(fields[4]) == camera["width"]
    assert int(fields[5]) == camera["height"]
    assert int(fields[8]) == camera["fps"]
    # dwMaxVideoFrameBufferSize, as the descriptor carries it. This is not the
    # dwMaxVideoFrameSize a Probe/Commit exchange would settle on -- for a
    # compressed format TinyUSB recomputes that as width * height * 2 and ignores
    # the descriptor -- so the two must not be asserted against each other once
    # streaming exists.
    assert int(fields[13]) == camera["max_frame"]

    # UVC numbers formats and frames from 1; a zero here means the index was not
    # decoded, and Probe/Commit with it would be refused by the camera.
    assert int(fields[6]) == 1, "bFormatIndex not decoded"
    assert int(fields[7]) == 1, "bFrameIndex not decoded"

    # EspUsbDevice writes its single fixed rate as the *continuous* form with
    # min == max and step 0, not as a one-entry discrete list. The host must
    # decode it that way round: a discrete count here would mean it read the
    # min/max/step bytes as a list of intervals.
    interval = 10000000 // camera["fps"]
    assert int(fields[9]) == 0, "a continuous frame descriptor was decoded as discrete"
    assert int(fields[10]) == interval, "dwMinFrameInterval"
    assert int(fields[11]) == interval, "dwMaxFrameInterval"
    assert int(fields[12]) == 0, "dwFrameIntervalStep"

    # The alternate setting's bandwidth, multiplied out of wMaxPacketSize. The
    # peer reports the packet size it asked for at full speed, and at full speed
    # there is no high-bandwidth multiplier, so the two are the same number.
    assert int(fields[14]) == camera["packet"]
    assert int(fields[15]) == 1, "the discovered format is not startable"

    dut.expect_exact("VIDEO_STREAM_COUNT 1")


def test_selection_resolves_what_the_camera_offers(dut, camera):
    """espUsbHostSelectVideoStream() picks the stream for a caller's request."""
    # No preference at all.
    dut.write("a")
    any_match = dut.expect(
        r"VIDEO_SELECT any found=1 format=MJPEG (\d+)x(\d+) frameIndex=1 fps=(\d+)\r?\n")
    assert int(any_match.group(1)) == camera["width"]
    assert int(any_match.group(2)) == camera["height"]
    assert int(any_match.group(3)) == camera["fps"]

    # Fully specified: the same stream, reached the other way.
    dut.write("m")
    dut.expect(r"VIDEO_SELECT mjpeg-320x240-15 found=1 format=MJPEG 320x240 frameIndex=1 fps=15\r?\n")


def test_selection_refuses_what_the_camera_does_not_offer(dut, camera):
    """A request the camera cannot serve is refused, not substituted.

    This is the assertion that matters most for a caller's safety: a host that
    quietly returned 320x240 for a 1920x1080 request would have the sketch size
    its frame buffer for one and receive the other.
    """
    dut.write("x")
    dut.expect_exact("VIDEO_SELECT 1920x1080 found=0")

    dut.write("y")
    dut.expect_exact("VIDEO_SELECT mjpeg-320x240-60 found=0")

    dut.write("z")
    dut.expect_exact("VIDEO_SELECT yuy2 found=0")


def test_streaming_delivers_whole_frames(dut, peers, camera):
    """Probe/Commit, alternate selection, and frame assembly end to end."""
    device = peers["device"]

    dut.write("S")
    start = dut.expect(
        r"VIDEO_START started=(\d) committed=(\d) format=(\d+) frame=(\d+) interval=(\d+) "
        r"frame_size=(\d+) payload=(\d+) error=(\S+)\r?\n")
    assert start.group(1) == b"1", f"videoStart() failed: {start.group(8).decode()}"
    assert start.group(2) == b"1", "nothing was committed"

    # The camera must have committed to the format and frame that were asked for.
    # A camera is allowed to answer with different ones, and the host commits what
    # it answered -- so this asserts the peer agreed, not that the host insisted.
    assert int(start.group(3)) == 1, "committed bFormatIndex"
    assert int(start.group(4)) == 1, "committed bFrameIndex"
    assert int(start.group(5)) == 10000000 // camera["fps"], "committed dwFrameInterval"

    # dwMaxPayloadTransferSize decides which alternate the host had to select.
    # Zero would mean the camera never told the host how much it would send, which
    # videoStart() refuses rather than guessing at.
    assert int(start.group(7)) > 0, "committed dwMaxPayloadTransferSize"

    device.expect_exact("DEVICE_VIDEO_STREAMING 1")

    # Let frames flow. At 15 fps this is about 30 frames; the assertions below are
    # deliberately far below that so a slow bench does not fail the test.
    time.sleep(2)

    dut.write("F")
    frames = dut.expect(
        r"VIDEO_FRAMES have=1 streaming=1 seen=(\d+) good=(\d+) incomplete=(\d+) "
        r"last_len=(\d+) mismatch=(\d) at=(\d+)\r?\n")
    stats = dut.expect(
        r"VIDEO_STATS frames=(\d+) incomplete=(\d+) payloads=(\d+) header_errors=(\d+) "
        r"payload_errors=(\d+) packet_errors=(\d+) overflows=(\d+) bytes=(\d+)\r?\n")

    seen = int(frames.group(1))
    good = int(frames.group(2))
    incomplete = int(frames.group(3))
    last_len = int(frames.group(4))

    assert seen >= 5, f"only {seen} frames arrived in 2 s"
    assert frames.group(5) == b"0", (
        f"frame contents diverged from the pattern at offset {int(frames.group(6))}; "
        f"the payloads were not reassembled correctly")
    assert good >= 5, f"{good} of {seen} frames matched the pattern"
    assert last_len == camera["frame_bytes"], (
        f"the host assembled {last_len} bytes for a frame the peer sent as "
        f"{camera['frame_bytes']}")

    # A frame spans many payloads, so the payload count must be several times the
    # frame count. If it were not, the host would be treating one payload as one
    # frame -- which would still produce frames, just wrong ones.
    payloads = int(stats.group(3))
    assert payloads > seen * 4, (
        f"{payloads} payloads for {seen} frames: a frame of "
        f"{camera['frame_bytes']} bytes cannot fit in that few")

    # Nothing should be damaged on a direct two-board link. These are the counters
    # that separate a working stream from one that happens to produce frames.
    assert int(stats.group(4)) == 0, "payload headers failed to decode"
    assert int(stats.group(5)) == 0, "the camera flagged payloads as bad"
    assert int(stats.group(6)) == 0, "the host controller reported failed isochronous packets"
    assert int(stats.group(7)) == 0, "frames overran the frame buffer"
    assert incomplete == 0, f"{incomplete} of {seen} frames were incomplete"
    assert int(stats.group(8)) >= seen * camera["frame_bytes"] * 0.9

    # The peer counted frames on its side too; the two must be in the same range.
    device.write("q")
    sent = device.expect(r"DEVICE_VIDEO_STATE streaming=1 sent=(\d+)\r?\n")
    assert int(sent.group(1)) >= seen, (
        f"the host says it received {seen} frames but the peer only sent "
        f"{int(sent.group(1))}")


def test_streaming_stops_cleanly(dut, peers, camera):
    """videoStop() puts the interface back on its idle alternate.

    The camera has to see the stop: an isochronous alternate holds its reserved
    bandwidth for as long as it is selected, so a host that freed its own state
    without selecting alternate 0 would leave the camera sending into nothing.
    """
    device = peers["device"]

    dut.write("S")
    dut.expect(r"VIDEO_START started=1 .*\r?\n")
    device.expect_exact("DEVICE_VIDEO_STREAMING 1")
    time.sleep(0.5)

    dut.write("T")
    dut.expect_exact("VIDEO_STOP stopped=1 streaming=0")
    device.expect_exact("DEVICE_VIDEO_STREAMING 0")

    # And it can be started again afterwards, which is what a sketch changing
    # resolution has to do.
    dut.write("S")
    dut.expect(r"VIDEO_START started=1 .*\r?\n")
    device.expect_exact("DEVICE_VIDEO_STREAMING 1")
    time.sleep(1)
    dut.write("F")
    frames = dut.expect(r"VIDEO_FRAMES have=1 streaming=1 seen=(\d+) good=(\d+) .*\r?\n")
    assert int(frames.group(2)) >= 3, "no good frames after restarting the stream"

    dut.write("T")
    dut.expect_exact("VIDEO_STOP stopped=1 streaming=0")


def test_camera_can_be_replugged_while_streaming(dut, peers, camera):
    """A camera taken away mid-stream is enumerated again when it comes back.

    The disconnect path has to release the streaming interface it claimed, and
    releasing it is not optional just because the device is gone: the client's own
    claim outlives the device, ``usb_host_device_close()`` refuses while one is
    held, and the host then retries that close forever with the address still in
    use. The camera comes back and nothing happens -- no error, no callback, and
    the only way out is restarting the host.
    """
    device = peers["device"]

    dut.write("S")
    dut.expect(r"VIDEO_START started=1 .*\r?\n")
    device.expect_exact("DEVICE_VIDEO_STREAMING 1")
    time.sleep(0.5)

    # Take the camera away without stopping the stream first.
    device.write("X")
    device.expect_exact("DEVICE_VIDEO_DETACHED")
    dut.expect(r"DEVICE_DISCONNECTED addr=\d+\r?\n", timeout=10)

    device.write("Y")
    device.expect_exact("DEVICE_VIDEO_ATTACHED 1")

    # The host must find it again. Polling rather than waiting on the connect
    # line, because that line is printed once.
    _poll_state(dut, r"VIDEO_DEVICE addr=[1-9]\d* streams=[1-9]\d*", command="v", attempts=60)

    # And it must be startable again, not merely visible.
    dut.write("S")
    start = dut.expect(r"VIDEO_START started=(\d) .*error=(\S+)\r?\n")
    assert start.group(1) == b"1", (
        f"the camera enumerated again but would not start: {start.group(2).decode()}")
    device.expect_exact("DEVICE_VIDEO_STREAMING 1")
    time.sleep(1)

    dut.write("F")
    frames = dut.expect(r"VIDEO_FRAMES have=1 streaming=1 seen=(\d+) good=(\d+) .*\r?\n")
    assert int(frames.group(2)) >= 3, "no good frames after the camera was plugged back in"

    dut.write("T")
    dut.expect_exact("VIDEO_STOP stopped=1 streaming=0")
