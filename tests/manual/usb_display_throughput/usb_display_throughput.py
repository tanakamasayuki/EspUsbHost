"""
Purpose:
    Sweep the tuning knobs of a DL-1xx USB graphics adapter driven through
    LovyanGFX and LGFXVirtualCanvas, and record what each one is worth on real
    hardware. Every condition renders for three seconds and reports the frame
    rate, what the diff transfer skipped, and what actually reached USB.

    Groups: A tile geometry, B double buffering, C diff transfer, D auto clear,
    E draw scope (whole screen vs a sprite over the changing part), F direct to
    panel with no tiling, G scene content.

    The point is the comparison, not any single number: this is where the
    "how do I make it faster" guidance in the example README comes from.

Why manual:
    Needs a physical adapter and monitor, and two of the conditions (group F) are
    judged by eye -- a full clear and redraw with no buffering is expected to
    flicker badly, while repainting only what moved should not.

Required hardware:
    - ESP32-S3 or ESP32-P4 host board
    - USB graphics adapter with a DisplayLink DL-1xx chip (VID 0x17e9)
    - A monitor attached to the adapter that supports 1920x1080
    - Power for the adapter: a self-powered hub or an external supply

Setup:
    1. Connect the host board to the PC.
    2. Attach the monitor to the adapter and the adapter to the board's USB host
       port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/usb_display_throughput/usb_display_throughput.py -v -s
    5. Watch the monitor during group F and note whether the flicker matches what
       the prompts describe.

Notes:
    The whole sweep takes about 70 seconds. usb_bps is reported as a share of the
    bulk OUT ceiling measured by manual/vendor_bulk_throughput on the same board,
    selected from --profile: 1.098 MB/s for full speed (ESP32-S3) and 36.4 MB/s
    for high speed (ESP32-P4).

    The ESP32-P4 run needs a self-powered hub -- the host port did not supply
    enough current for the adapter here -- but nothing else may share that hub. A
    device on it that fails its downstream port reset (a full-speed touch panel,
    in our case) makes ESP-IDF's own ext_port driver abort the whole host with
    "assert failed: handle_recycle ext_port.c" right after begin(). Nothing in
    this library can intercept that.
"""

import re

# Bulk OUT ceilings measured by manual/vendor_bulk_throughput on the same boards.
# The "bus" share is meaningless against the wrong one: an ESP32-P4 runs the
# adapter at high speed and exceeds the full-speed ceiling several times over.
CEILING_BPS_FULL_SPEED = 1151434  # 1.098 MB/s, ESP32-S3
CEILING_BPS_HIGH_SPEED = 38191924  # 36.4 MB/s, ESP32-P4


def _ceiling_bps(config) -> tuple[int, str]:
    profile = config.getoption("profile") or ""
    if "p4" in profile:
        return CEILING_BPS_HIGH_SPEED, "high speed"
    return CEILING_BPS_FULL_SPEED, "full speed"

SKIP = re.compile(
    r'DISPLAY_TUNE_SKIP id=(\S+) label="([^"]*)" reason=(\w+) mem=(\d+) dbuf=(\d)'
)

TUNE = re.compile(
    r'DISPLAY_TUNE id=(\S+) label="([^"]*)" mode=(\d+) tiles=(\d+) tile_h=(\d+) '
    r"diff=(\d) clear=(\d) dbuf=(-?\d+) frames=(\d+) seconds=([\d.]+) fps=([\d.]+) "
    r"pushed_px=(\d+) total_px=(\d+) usb_bytes=(\d+) usb_bps=(\d+) errors=(\d+)"
)


def test_usb_display_throughput(dut, request):
    """
    Expected result (pass):  Every condition completes with errors=0 and the
                             sketch prints "[PASS]". The table is printed for the
                             record.
    Expected result (fail):  A condition fails to allocate or render, or a
                             transfer error is reported.

    Operator check (not automated):
      - F1 (direct full redraw) flickers visibly
      - F2 (direct incremental) does not
    """
    dut.expect("usb_display_throughput test start")
    print("\nAttach a monitor to a DL-1xx adapter (VID 0x17e9) and connect it to the host port.")
    print("Watch the monitor during group F: a full clear and redraw should flicker.")

    ceiling_bps, ceiling_name = _ceiling_bps(request.config)

    ready = dut.expect(r"DISPLAY_TUNE_READY (\d+)x(\d+)", timeout=90)
    width, height = (int(g) for g in ready.groups())
    print(f"\npanel: {width}x{height}")
    print(f"bus share is against {ceiling_bps / 1024 / 1024:.1f} MB/s ({ceiling_name})")

    # A condition whose tile buffer does not fit reports SKIP instead of a result;
    # the memory ceiling is one of the things this sweep establishes.
    rows = []
    skipped = []
    while len(rows) + len(skipped) < 19:
        match = dut.expect([TUNE, SKIP], timeout=60)
        text = match.group(0).decode() if isinstance(match.group(0), bytes) else match.group(0)
        groups = [g.decode() if isinstance(g, bytes) else g for g in match.groups()]
        if "DISPLAY_TUNE_SKIP" in text:
            skipped.append(groups)
        else:
            rows.append(groups)

    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"

    print("\nid  label                tiles  h    fps    pushed%   usb KB/s  bus%  errors")
    for (cid, label, _mode, tiles, tile_h, _diff, _clear, _dbuf, _frames, _sec, fps,
         pushed, total, _bytes, bps, errors) in rows:
        share = f"{100.0 * int(pushed) / int(total):6.1f}" if int(total) else "     -"
        bus = 100.0 * int(bps) / ceiling_bps
        print(f"{cid:3} {label:20} {tiles:>5}  {tile_h:>3}  {float(fps):5.2f}  {share}   "
              f"{int(bps) / 1024:8.1f}  {bus:4.1f}  {errors:>3}")

    if skipped:
        print("\nskipped (tile buffer did not fit):")
        for cid, label, reason, mem, dbuf in skipped:
            print(f"  {cid} {label}: {reason}, {int(mem) // 1024} KB, double buffer {dbuf}")

    for row in rows:
        assert row[-1] == "0", f"condition {row[0]} reported {row[-1]} transfer error(s)"

    # Report the comparisons the example README quotes, so a re-run refreshes them.
    by_id = {row[0]: row for row in rows}
    print()
    for label, a, b in (
        ("tile geometry (default -> 256k)", "A1", "A5"),
        ("double buffer (on -> off)", "B1", "B2"),
        ("diff transfer (on -> off)", "C1", "C2"),
        ("auto clear (on -> off)", "D1", "D2"),
        ("draw scope (screen -> sprite)", "E1", "E2"),
        ("direct (full -> incremental)", "F1", "F2"),
    ):
        if a in by_id and b in by_id:
            fa = float(by_id[a][10])
            fb = float(by_id[b][10])
            ratio = f"{fb / fa:.2f}x" if fa > 0 else "n/a"
            print(f"{label:34} {fa:6.2f} -> {fb:6.2f} fps  ({ratio})")
