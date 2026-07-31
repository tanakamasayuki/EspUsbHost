"""
Purpose:
    Bring up a DL-1xx USB graphics adapter: claim the vendor interface, select the
    standard channel, read EDID, program 1920x1080 and paint solid fills, color
    bars and a 1px checkerboard. This is the first test that puts pixels on a real
    monitor, so it is what confirms the mode-set derivation and the timing
    register byte order (the LFSR registers are written high byte first, an
    assumption the published notes do not state explicitly).

    It also checks the two behaviors the display backend depends on: the image
    persists with zero USB traffic, and re-sending the mode registers is a valid
    recovery path.

Why manual:
    The result is on a monitor, so correctness has to be judged by eye. It also
    needs a physical adapter and display.

Required hardware:
    - ESP32-S3 or ESP32-P4 host board
    - USB graphics adapter with a DisplayLink DL-1xx chip (VID 0x17e9)
    - A monitor attached to the adapter that supports 1920x1080
    - Power for the adapter: a self-powered hub or an external supply, unless the
      board can feed its OTG connector

Setup:
    1. Connect the host board to the PC.
    2. Attach the monitor to the adapter and the adapter to the board's USB host
       port. Keep the chain minimal: the ESP32-S3 has only 8 host channels, and a
       hub adds one device of its own. When the adapter needs a self-powered hub
       for current -- required on the ESP32-P4 board here -- give it that hub to
       itself. A device on the same hub that fails its downstream port reset (a
       full-speed touch panel, in our case) makes ESP-IDF's ext_port driver abort
       the host with "assert failed: handle_recycle ext_port.c".
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/usb_display_dl1xx/usb_display_dl1xx.py -v -s
    5. Watch the monitor while the test runs and confirm what the prompts describe.

Notes:
    Each step holds for two seconds so the images can be seen. The full EDID block is
    dumped as hex for the record.
"""

import re

PAINT = re.compile(
    r"DISPLAY_PAINT what=(\w+) elapsed_us=(\d+) tx_bytes=(\d+) mbps=([\d.]+) errors=(\d+)"
)


def test_usb_display_dl1xx(dut):
    """
    Expected result (pass):  The adapter opens, EDID reads back with a valid
                             header and checksum, 1920x1080 is programmed, every
                             paint step reports errors=0, and the sketch prints
                             "[PASS]".
    Expected result (fail):  Opening, the mode set, or a paint step fails.

    Operator checks (not automated):
      - solid red, then green, then blue fill the whole screen
      - eight vertical color bars appear (white, yellow, cyan, green, magenta,
        red, blue, black)
      - a fine 1px checkerboard appears
      - the image is unchanged after 3 seconds of zero USB traffic
      - the image survives a mode resend
    """
    dut.expect("usb_display_dl1xx test start")
    print("\nAttach a monitor to a DL-1xx adapter (VID 0x17e9) and connect it to the host port.")
    print("Watch the monitor: solid fills, then color bars, then a 1px checkerboard.")

    dut.expect("DISPLAY_OPEN", timeout=90)
    edid = dut.expect(r"DISPLAY_EDID read=(\d) valid=(\d) checksum=(\d)", timeout=30)
    read_ok, valid, checksum = (g.decode() for g in edid.groups())
    print(f"\nEDID: read={read_ok} valid={valid} checksum={checksum}")

    dut.expect("DISPLAY_MODE_SET", timeout=30)

    paints = []
    for _ in range(7):  # mode_set + 3 fills + bars + checkerboard + mode_resend
        match = dut.expect(PAINT, timeout=120)
        paints.append([g.decode() for g in match.groups()])

    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"

    print("\nwhat            ms     tx_bytes    MB/s   errors")
    for what, us, tx, mbps, errors in paints:
        print(f"{what:14} {int(us) / 1000:7.1f}  {int(tx):9}  {float(mbps):6.3f}  {errors:>3}")

    for what, _us, _tx, _mbps, errors in paints:
        assert errors == "0", f"{what} reported {errors} transfer error(s)"

    assert read_ok == "1", "EDID could not be read"
    assert valid == "1", "the EDID header is not valid"
    assert checksum == "1", "the EDID checksum does not match"
