"""
Purpose:
    Measure the effective vendor bulk IN throughput and separate the two things
    that can limit it: how many bytes one transfer asks for, and how many
    transfers are outstanding at once. The sketch reads 1 MiB per condition --
    first over the continuous read vendorOpen() sets up (one max-size packet per
    transfer), then over the asynchronous read queue at depths 1 / 2 / 4 and
    transfer sizes 512 B / 2 KB / 8 KB / 16 KB / 32 KB -- and verifies the ramp
    the peer sends arrives unbroken in every condition.

    Read the table, not a single number. Depth 1 at a large transfer size and
    depth 2+ at a small one answer different questions: the first says whether
    the endpoint was starved by the transfer size, the second whether it was
    starved by the turnaround between transfers. `starved` counts the completions
    that found nothing else in flight, and `per_transfer` is how much the device
    actually put into each transfer, which is what tells a device-side supply
    limit apart from a host-side one.

Why manual:
    Requires a second board streaming on a vendor bulk IN endpoint, and the
    result is a measurement rather than a pass/fail property. Absolute throughput
    depends on both boards, the port speed, and the bus.

Required hardware:
    - ESP32-P4 (high speed) or ESP32-S3 (full speed) host board
    - A second board flashed with tests/peer/usb_vendor_read/peer_device, which
      answers 'S' + a 4-byte little-endian length with that many bytes of a
      0..255 ramp. Build it for whichever board is acting as the device.

Setup:
    1. Connect the host board to the PC.
    2. Connect the device board to the host board's USB host port. On an
       ESP32-P4, use the OTG HS port on both for high-speed numbers.
    3. Set TEST_SERIAL_PORT_ESP32P4 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/vendor_bulk_in_throughput/vendor_bulk_in_throughput.py -v -s

Notes:
    bad=0 in every row is the integrity check: the ramp is continuous across the
    whole stream, so a dropped or reordered transfer shows up as a step.
"""

import re

ROW = re.compile(
    r"VENDOR_BULK_IN_THROUGHPUT mode=([\w-]+) depth=(\d+) xfer=(\d+) bytes=(\d+) chunks=(\d+) "
    r"max_chunk=(\d+) bad=(\d+) elapsed_us=(\d+) mbps=([\d.]+) completed=(\d+) errors=(\d+) "
    r"short=(\d+) starved=(\d+) per_transfer=([\d.]+)"
)

DEPTHS = (1, 2, 4)
SIZES = (512, 2048, 8192, 16384, 32768)


def test_vendor_bulk_in_throughput(dut):
    """
    Expected result (pass):  Every condition delivers the full 1 MiB with bad=0
                             and errors=0, the queue reopens after end(), and the
                             sketch prints "[PASS]". The measured table is printed
                             for the record.
    Expected result (fail):  A condition stalls or comes up short, the ramp breaks
                             (bad>0), or the continuous read does not come back
                             after the queue is ended.
    """
    dut.expect("vendor_bulk_in_throughput test start")
    print("\nConnect a board flashed with tests/peer/usb_vendor_read/peer_device to the host port.")
    dut.expect("VENDOR_OPEN ok=1", timeout=90)

    rows = []
    # continuous baseline + depths x sizes + the reopened continuous read.
    for _ in range(1 + len(DEPTHS) * len(SIZES) + 1):
        match = dut.expect(ROW, timeout=120)
        rows.append([g.decode() if isinstance(g, bytes) else g for g in match.groups()])

    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"

    print("\nmode              depth   xfer     MB/s  per_transfer  starved  short")
    for mode, depth, xfer, _b, _c, _mc, _bad, _us, mbps, _comp, _err, short, starved, per in rows:
        print(f"{mode:16} {depth:>5}  {xfer:>5}  {float(mbps):7.3f}  {float(per):12.1f}  "
              f"{starved:>7}  {short:>5}")

    baseline = float(rows[0][8])
    best_queue = max(float(r[8]) for r in rows if r[0] == "queue")
    print(f"\ncontinuous (one packet per transfer) = {baseline:.3f} MB/s"
          f"  best queue = {best_queue:.3f} MB/s")

    # Splitting the two effects apart is the point of the sweep, so report both
    # slices rather than only the best cell.
    deep_small = max(float(r[8]) for r in rows if r[0] == "queue" and int(r[1]) >= 2 and int(r[2]) == SIZES[0])
    shallow_large = max(float(r[8]) for r in rows if r[0] == "queue" and int(r[1]) == 1 and int(r[2]) >= 8192)
    print(f"depth>=2 at {SIZES[0]} B = {deep_small:.3f} MB/s"
          f"   depth 1 at >=8 KB = {shallow_large:.3f} MB/s")

    assert all(int(r[6]) == 0 for r in rows), "the ramp must arrive unbroken in every condition"
    assert all(int(r[10]) == 0 for r in rows), "no condition should record a transfer error"
    assert best_queue > baseline * 1.05, (
        f"the read queue ({best_queue:.3f} MB/s) should beat the one-packet continuous "
        f"read ({baseline:.3f} MB/s)"
    )
