"""
Purpose:
    Measure the effective vendor bulk OUT throughput and confirm the
    asynchronous queue beats the synchronous vendorWrite() baseline. The sketch
    pushes 256 KB per condition over transfer sizes 512 B / 2 KB / 8 KB / 16 KB,
    first with vendorWrite() and then with queue depths 1 / 2 / 4 / 8, and also
    checks the queue's slot accounting, oversized-payload rejection, and reuse
    after vendorWriteQueueEnd().

    The number this run establishes -- the practical bulk OUT ceiling of the
    board -- is what later display work is normalized against, so record the
    table. Measured peaks: 1.098 MB/s on an ESP32-S3 (full speed, from a queue
    depth of 2 at every transfer size) and 36.4 MB/s on an ESP32-P4 (high speed,
    async depth 2 with 8 KB transfers).

Why manual:
    Requires a physical device with a vendor-specific bulk OUT endpoint, and the
    result is a measurement rather than a pass/fail property. Absolute
    throughput depends on the host board, the attached device, and bus
    conditions, so no fixed threshold is asserted.

Required hardware:
    - ESP32-S3 or ESP32-P4 host board
    - A device with a vendor-specific (0xff) interface exposing a bulk OUT
      endpoint. A USB graphics adapter (DisplayLink DL-1xx, VID 0x17e9) works and
      ignores the 0xAF filler bytes the sketch sends. An EspUsbDevice vendor peer
      also works.

Setup:
    1. Connect the host board to the PC.
    2. Connect the vendor device directly to the board's USB host port. Avoid
       hubs: the ESP32-S3 has only 8 host channels.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/vendor_bulk_throughput/vendor_bulk_throughput.py -v -s

Notes:
    The payload is 0xAF filler, which is padding (a no-op) for a DL-1xx adapter.
    Nothing is read back; this measures host-to-device only.

    queue_empty_pct is the share of iterations that found no transfer in flight.
    A high value means the producer (CPU) is the limit, a value near zero means
    the USB pipe is saturated.
"""

import re

ROW = re.compile(
    r"VENDOR_BULK_THROUGHPUT mode=(\w+) depth=(\d+) xfer=(\d+) bytes=(\d+) "
    r"elapsed_us=(\d+) mbps=([\d.]+) submit_fail=(\d+) errors=(\d+) "
    r"queue_full=(\d+) queue_empty_pct=(\d+)"
)


def test_vendor_bulk_throughput(dut):
    """
    Expected result (pass):  Every condition completes with submit_fail=0 and
                             errors=0, the queue self-checks hold, and the sketch
                             prints "[PASS]". The measured table is printed for
                             the record.
    Expected result (fail):  A condition stalls, a submit or transfer fails, or a
                             queue accounting check does not hold.
    """
    dut.expect("vendor_bulk_throughput test start")
    print("\nConnect a vendor-class bulk OUT device (e.g. VID 0x17e9) to the host port.")
    dut.expect("VENDOR_OPEN ok=1", timeout=90)

    rows = []
    # 4 synchronous conditions + 4 depths x 4 transfer sizes.
    for _ in range(4 + 4 * 4):
        match = dut.expect(ROW, timeout=120)
        rows.append([g.decode() if isinstance(g, bytes) else g for g in match.groups()])

    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"

    print("\nmode   depth  xfer    MB/s    queue_empty%")
    for mode, depth, xfer, _bytes, _us, mbps, _sf, _err, _qf, empty in rows:
        print(f"{mode:6} {depth:>5}  {xfer:>5}  {float(mbps):6.3f}  {empty:>3}")

    best_sync = max(float(r[5]) for r in rows if r[0] == "sync")
    best_async = max(float(r[5]) for r in rows if r[0] == "async")
    print(f"\nbest sync={best_sync:.3f} MB/s  best async={best_async:.3f} MB/s")

    # Compare like for like. At the largest transfer size both modes approach the
    # bus ceiling, so the queue's advantage only shows where per-transfer latency
    # dominates: the smallest transfer size. That is also the regime a display
    # backend lands in, since RLE command chunks are small.
    small = str(min(int(r[2]) for r in rows))
    sync_small = next(float(r[5]) for r in rows if r[0] == "sync" and r[2] == small)
    async_small = max(
        float(r[5]) for r in rows if r[0] == "async" and r[2] == small and int(r[1]) >= 2
    )
    print(f"at xfer={small}: sync={sync_small:.3f} MB/s  async(depth>=2)={async_small:.3f} MB/s")
    assert async_small > sync_small * 1.05, (
        f"at {small}-byte transfers the async queue ({async_small:.3f} MB/s) should "
        f"clearly beat the synchronous baseline ({sync_small:.3f} MB/s)"
    )
