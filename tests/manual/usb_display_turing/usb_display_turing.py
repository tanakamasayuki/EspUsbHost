"""
Purpose:
    Bring up a 3.5-inch USB smart screen (VID 0x1a86 PID 0x5722): open the CDC OUT
    queue, set the orientation, and paint solid fills, primary patches, color bars,
    a 1px checkerboard and a partial rectangle. This is the test that confirms the
    6-byte command packing, the RGB565 little-endian pixel order and the
    orientation command against real hardware.

    It also checks the three behaviors the LovyanGFX backend depends on: the image
    persists with zero USB traffic, a partial rectangle updates only its own area,
    and the same full screen costs the same time however it is split into
    rectangles -- the last one being the guard against silently dropped
    rectangles, described below.

Why manual:
    The result is on a panel, so correctness has to be judged by eye. It also needs
    the physical display.

Required hardware:
    - ESP32-S3 or ESP32-P4 host board
    - A 3.5-inch USB smart screen enumerating as 1a86:5722 (product "UsbMonitor")

Setup:
    1. Connect the host board to the PC.
    2. Connect the panel to the board's USB host port. It is bus-powered and the
       backlight draws real current, so use a self-powered hub or an external
       supply unless the board can feed its OTG connector.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/usb_display_turing/usb_display_turing.py -v -s
    5. Watch the panel while the test runs and confirm what the prompts describe.

Notes:
    Each step holds for two seconds so the images can be seen. The protocol has no
    compression, so every full-screen paint is 320 * 480 * 2 = 307,200 bytes, and
    the panel renders at about 0.155 MB/s regardless of how that is split up.

    The band sweep is a regression guard, not a tuning measurement. A command that
    arrives before the previous rectangle's pixels have landed is discarded by the
    panel, which keeps consuming the bytes either way -- so the failure is
    invisible in the error counters and shows up only as the sweep getting
    *faster* with more rectangles (0.156 MB/s at one rectangle against 0.403 MB/s
    at 24 when the extra 23 are being thrown away).
"""

import re

PAINT = re.compile(
    r"DISPLAY_PAINT what=(\w+) elapsed_us=(\d+) tx_bytes=(\d+) mbps=([\d.]+) "
    r"errors=(\d+) queue_full=(\d+)"
)
ORIENTATION = re.compile(r"DISPLAY_ORIENTATION name=(\w+) (\d+)x(\d+)")
SPLIT = re.compile(r"DISPLAY_SPLIT bands=(\d+) elapsed_us=(\d+) tx_bytes=(\d+) mbps=([\d.]+)")


def test_usb_display_turing(dut):
    """
    Expected result (pass):  The panel opens, the CDC OUT queue is ready, both
                             orientations report the expected size, every paint
                             step reports errors=0, and the sketch prints "[PASS]".
    Expected result (fail):  Opening, an orientation change, or a paint step fails.

    Operator checks (not automated):
      - solid red, then green, then blue fill the whole panel
      - three horizontal bands appear: red, green, blue from top to bottom
      - eight vertical color bars appear (white, yellow, cyan, green, magenta,
        red, blue, black)
      - a fine 1px checkerboard appears
      - one yellow rectangle covers the middle of the checkerboard, and the rest
        of the checkerboard is untouched
      - the image is unchanged after 3 seconds of zero USB traffic
      - the backlight dims and comes back
      - the color bars run along the long edge in landscape, and along the short
        edge again in portrait
    """
    dut.expect("usb_display_turing test start")
    print("\nConnect a 3.5-inch USB smart screen (1a86:5722) to the host port.")
    print("Watch the panel: solid fills, primary bands, color bars, checkerboard.")

    open_line = dut.expect(r"DISPLAY_OPEN address=(\d+) out_mps=(\d+) queue_ready=(\d)", timeout=90)
    address, mps, queue_ready = (g.decode() for g in open_line.groups())
    print(f"\nopened: address={address} out_mps={mps} queue_ready={queue_ready}")
    assert queue_ready == "1", "the CDC OUT queue did not start"

    portrait = dut.expect(ORIENTATION, timeout=30)
    name, width, height = (g.decode() for g in portrait.groups())
    assert (name, width, height) == ("portrait", "320", "480"), f"unexpected portrait size {width}x{height}"

    paints = []
    for _ in range(7):  # 3 fills + patches + bars + checkerboard + partial rect
        match = dut.expect(PAINT, timeout=120)
        paints.append([g.decode() for g in match.groups()])

    # The same full screen painted as 1, 3, 8 and 24 rectangles. Identical byte
    # counts, so the spread is the panel's per-rectangle behavior.
    splits = []
    for _ in range(6):
        match = dut.expect(SPLIT, timeout=120)
        splits.append([g.decode() for g in match.groups()])

    dut.expect("DISPLAY_PERSIST", timeout=60)

    landscape = dut.expect(ORIENTATION, timeout=60)
    name, width, height = (g.decode() for g in landscape.groups())
    assert (name, width, height) == ("landscape", "480", "320"), f"unexpected landscape size {width}x{height}"
    paints.append([g.decode() for g in dut.expect(PAINT, timeout=120).groups()])

    back = dut.expect(ORIENTATION, timeout=60)
    name, width, height = (g.decode() for g in back.groups())
    assert (name, width, height) == ("portrait", "320", "480"), f"unexpected portrait size {width}x{height}"

    print("\nbands  elapsed_us    tx_bytes   MB/s")
    for bands, elapsed, tx, mbps in splits:
        print(f"{bands:>5s} {elapsed:>11s} {tx:>11s} {mbps:>6s}")

    print("\nwhat                  elapsed_us    tx_bytes   MB/s  errors  queue_full")
    for what, elapsed, tx, mbps, errors, queue_full in paints:
        print(f"{what:20s} {elapsed:>11s} {tx:>11s} {mbps:>6s} {errors:>7s} {queue_full:>11s}")

    for what, _elapsed, _tx, _mbps, errors, _queue_full in paints:
        assert errors == "0", f"{what} reported {errors} USB transfer errors"

    # Every split sends the same bytes, and the panel renders at its own fixed
    # rate, so the rates must agree. They only diverge if rectangles are being
    # dropped instead of drawn -- which is what happens when a command is sent
    # before the previous rectangle's pixels have landed, and it is invisible
    # in the error counters because the panel still consumes the bytes.
    rates = [float(mbps) for _bands, _elapsed, _tx, mbps in splits]
    assert max(rates) / min(rates) < 1.2, (
        f"split rates diverge ({rates}); rectangles are being dropped, not drawn"
    )

    final = dut.expect(r"DISPLAY_GENERATION (\d+) underfilled=(\d+) dropped=(\d+)", timeout=30)
    _generation, underfilled, dropped = (g.decode() for g in final.groups())
    assert underfilled == "0", f"{underfilled} rectangle(s) were left short and had to be padded"
    assert dropped == "0", f"{dropped} write(s) never reached USB"
    dut.expect("[PASS]", timeout=30)
