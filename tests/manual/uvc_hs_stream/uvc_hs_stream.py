"""
Purpose:
    Measure UVC isochronous streaming on the ESP32-P4 high-speed port, and settle
    the one question the full-speed pairing cannot answer.

    A full-speed host cannot receive isochronous IN packets much above 120 bytes:
    arduino-esp32 builds the host stack with
    CONFIG_USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT, and what that leaves for IN on a
    256-line full-speed FIFO works out to (32 - 2) * 4. The high-speed port has
    1024 lines and the same default is supposed to leave 2552 bytes, but that was
    arithmetic rather than a measurement -- see
    docs/usb-host-advanced.md#53-isochronous-in-on-a-full-speed-port.

    The camera here asks for 1023-byte packets, which is eight times the
    full-speed ceiling, so whether the stream runs at all is the answer. What it
    is worth is the second question, and the peer steps through three frame
    lengths so one run separates per-frame cost from the packet rate.

Why manual:
    The two ESP32-P4 boards are wired OTG HS to OTG HS with an A-A cable and are
    not a permanent fixture of the bench, so this is in no CI job. The result is a
    measurement rather than a pass/fail property: absolute throughput depends on
    both boards and on what else the host controller is carrying.

Required hardware:
    - Two ESP32-P4 boards wired OTG HS to OTG HS
    - The device board flashed with peer_device/ from this directory. It is a
      separate sketch from tests/peer/usb_video/peer_device on purpose: that one
      pins its isochronous payload to 112 bytes for a full-speed host, and
      build_opt.h reaches every profile of the sketch it sits beside.

Setup:
    1. Flash the device board:
           arduino-cli compile --clean --profile p4_hs_direct_device --upload \
               -p $P4_HS_DIRECT_DEVICE_PORT tests/manual/uvc_hs_stream/peer_device
    2. Run:
           uv run --env-file .env pytest \
               manual/uvc_hs_stream/uvc_hs_stream.py -v -s --profile p4_hs_direct

    Nothing opens the device board's serial port. Its console is a native USB CDC
    and opening one resets the board, so the peer cycles the frame length on its
    own schedule and the host buckets what arrives by length. Neither side needs
    to know the other's timing.

Notes:
    payload= in UVC_HS_START is the number that matters most. 1023 means the
    high-speed port carries what a camera asks for; anything near 120 would mean
    the full-speed limit applies here too.

    mismatch=0 is the integrity check. Frames carry buffer[i] = i + n, so a
    payload header left in the image data, a dropped payload or two frames joined
    together each break the pattern -- and none of those would show up in a byte
    count.

    packet_errors counts isochronous packets the host controller reported as
    failed. They are never retried, so anything above zero is lost image data.
"""

import re
import time

import pexpect
import pytest

START = re.compile(
    r"UVC_HS_START started=(\d) payload=(\d+) frame_size=(\d+) interval=(\d+) error=(\S+)"
)
ROW = re.compile(
    r"UVC_HS_ROW frame_len=(\d+) frames=(\d+) good=(\d+) bytes=(\d+) elapsed_ms=(\d+) "
    r"mbps=([\d.]+) fps=([\d.]+)"
)
SUMMARY = re.compile(
    r"UVC_HS frames=(\d+) incomplete=(\d+) lengths=(\d+) mismatch=(\d) at=(\d+) len=(\d+)"
)
STATS = re.compile(
    r"UVC_HS_STATS payloads=(\d+) header_errors=(\d+) payload_errors=(\d+) "
    r"packet_errors=(\d+) overflows=(\d+)"
)

# Long enough for the peer to hold each of its three frame lengths (4 s each) and
# for the first and last to be timed.
STREAM_SECONDS = 14


def _poll(dut, pattern, command, attempts=60):
    """Wait for a state the sketch answers on demand.

    Polling a query rather than waiting for a connect line matters because a
    connect line is printed once, when the device enumerates.
    """
    for _ in range(attempts):
        dut.write(command)
        try:
            dut.expect(pattern, timeout=2)
            return
        except Exception:
            continue
    raise AssertionError(f"the host never reported {pattern!r}")


def test_uvc_hs_stream(dut):
    # The board has been running since it was flashed; drop what it said before
    # this test body started so a stale line cannot desynchronise the commands.
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            dut.expect(r"[\s\S]+", timeout=0.5)
        except pexpect.TIMEOUT:
            break

    _poll(dut, r"HOST_STATE (?:idle|running) devices=\d+", "Q")
    dut.write("G")
    # Wait for the camera specifically. An ESP32-P4 presents its ROM
    # USB-Serial/JTAG unit on the same connector until the peer sketch calls
    # device.begin(), so a device count is not enough.
    _poll(dut, r"VIDEO_DEVICE addr=[1-9]\d* streams=[1-9]\d*", "v", attempts=60)

    dut.write("d")
    dut.expect(r"VIDEO_STREAM_COUNT [1-9]\d*")

    dut.write("S")
    start = START.search(dut.expect(START).group(0).decode())
    assert start.group(1) == "1", f"videoStart() failed: {start.group(5)}"
    payload = int(start.group(2))
    print(f"\nnegotiated payload = {payload} bytes, "
          f"frame interval = {start.group(4)} (100 ns units)")

    # The whole point of the run. A full-speed host cannot get past ~120 bytes
    # here; if this is still in that range, the high-speed port has the same
    # limit and the rest of the numbers are not worth reading.
    assert payload > 512, (
        f"the camera committed to {payload}-byte payloads: the high-speed port is "
        f"subject to the same periodic IN limit as full speed")

    time.sleep(STREAM_SECONDS)

    dut.write("M")
    rows = []
    for _ in range(3):
        try:
            rows.append(ROW.search(dut.expect(ROW, timeout=5).group(0).decode()))
        except Exception:
            break
    summary = SUMMARY.search(dut.expect(SUMMARY, timeout=5).group(0).decode())
    stats = STATS.search(dut.expect(STATS, timeout=5).group(0).decode())

    dut.write("T")
    dut.expect_exact("UVC_HS_STOP stopped=1")

    print(f"\n{'frame_len':>10} {'frames':>7} {'good':>6} {'MB/s':>8} {'fps':>7}")
    best = 0.0
    for row in rows:
        mbps = float(row.group(6))
        best = max(best, mbps)
        print(f"{int(row.group(1)):>10} {int(row.group(2)):>7} {int(row.group(3)):>6} "
              f"{mbps:>8.3f} {float(row.group(7)):>7.2f}")
    print(f"\npayloads={stats.group(1)} header_errors={stats.group(2)} "
          f"payload_errors={stats.group(3)} packet_errors={stats.group(4)} "
          f"overflows={stats.group(5)}")
    print(f"best = {best:.3f} MB/s")

    assert rows, "no frames arrived"
    assert summary.group(4) == "0", (
        f"frame contents diverged from the pattern at offset {int(summary.group(5))} "
        f"of a {int(summary.group(6))}-byte frame; the payloads were not reassembled "
        f"correctly")
    for row in rows:
        assert int(row.group(3)) == int(row.group(2)), (
            f"{int(row.group(2)) - int(row.group(3))} frames of "
            f"{int(row.group(1))} bytes did not match the pattern")
    assert int(stats.group(2)) == 0, "payload headers failed to decode"
    # A packet or two at stream start is expected and not a defect: the camera's
    # endpoint becomes active a microframe or so after it acknowledges
    # SET_INTERFACE, and the transfer armed immediately afterwards catches the
    # gap. One, repeatably, is what this rig produces. A systematic failure looks
    # nothing like this -- when the packet size is too large for the host's FIFO,
    # every packet of every transfer fails.
    packet_errors = int(stats.group(4))
    assert packet_errors <= 2, (
        f"{packet_errors} isochronous packets failed out of {int(stats.group(1))} "
        f"payloads; more than the handful expected at stream start")
    assert int(stats.group(5)) == 0, "frames overran the frame buffer"
    assert int(summary.group(2)) == 0, f"{int(summary.group(2))} frames were incomplete"
