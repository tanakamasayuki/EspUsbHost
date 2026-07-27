"""
Purpose:
    Detect CPU-cache / USB-DMA incoherency on MSC bulk IN transfers. The sketch
    reads one static LBA range twice with different transfer shapes while a
    background task thrashes the data cache; the media never changes, so any
    difference is corruption. On ESP32-P4 the corrupted regions are expected to
    start on 64-byte (L1 cache line) boundaries.

Why manual:
    Requires a physical USB storage device, and the failure is timing dependent.

Required hardware:
    - ESP32-P4 host board (the ESP32-S3 has no cached DMA memory and is only
      useful as a negative control)
    - USB flash drive or card reader connected to the host port

Setup:
    1. Connect the board to the host PC.
    2. Connect USB storage to the board's USB host port.
    3. Set TEST_SERIAL_PORT_ESP32P4 in .env.
    4. Run: uv run --env-file .env pytest manual/msc_cache_coherency/msc_cache_coherency.py -v -s

Notes:
    The test only reads; it never writes to the device.
"""


def test_msc_cache_coherency(dut):
    """
    Expected result (pass):  Every multi-sector read matches the single-sector
                             reference and the sketch prints "[PASS]".
    Expected result (fail):  MSC_CACHE_MISMATCH lines and "[FAIL]", meaning USB
                             DMA data was overwritten by the CPU cache.
    """
    dut.expect("MSC_CACHE_READY")
    dut.expect("MSC_DEVICE", timeout=30)
    dut.expect("MSC_CACHE_REFERENCE ok=1", timeout=60)
    dut.expect("MSC_CACHE_RESULT", timeout=120)
    dut.expect(r"\[PASS\]", timeout=10)
