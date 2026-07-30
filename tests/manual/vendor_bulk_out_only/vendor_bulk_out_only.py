"""
Purpose:
    Verify that vendorOpen() accepts a vendor-specific (0xff) interface whose
    only bulk endpoint is a bulk OUT, that vendorOutPacketSize() reports the
    endpoint max packet size, that vendorInPacketSize() stays 0, and that the
    endpoint channel count grows by exactly 1 instead of 2. Before the OUT-only
    fallback, vendorOpen() failed with "no bulk IN/OUT pair" on such devices.

    The run also dumps the interface and endpoint layout, so it doubles as a
    descriptor survey of the attached adapter.

Why manual:
    Requires a physical device whose vendor-specific interface has a bulk OUT
    but no bulk IN. The automated peer device (EspUsbDevice vendor) always
    exposes a bulk IN/OUT pair, so it cannot cover this case.

Required hardware:
    - ESP32-S3 or ESP32-P4 host board
    - A USB graphics adapter (DisplayLink DL-1xx family, VID 0x17e9) or any
      other device whose 0xff interface has a bulk OUT and no bulk IN

Setup:
    1. Connect the host board to the PC.
    2. Connect the adapter directly to the board's USB host port. Avoid hubs:
       the ESP32-S3 has only 8 host channels and a hub chain can exhaust them.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/vendor_bulk_out_only/vendor_bulk_out_only.py -v -s

Notes:
    The test only reads descriptors and claims the interface. It sends no
    payload to the device, so a display adapter shows nothing.
"""


def test_vendor_bulk_out_only(dut):
    """
    Expected result (pass):  VENDOR_SURVEY reports bulk_out=1 bulk_in=0,
                             VENDOR_OPEN ok=1 with a non-zero out_mps, in_mps=0,
                             the channel count grows by 1, reopen is idempotent,
                             and the sketch prints "[PASS]".
    Expected result (fail):  vendorOpen() rejects the interface, the packet sizes
                             or channel accounting disagree with the descriptor,
                             or no vendor interface is found.
    """
    dut.expect("vendor_bulk_out_only test start")
    print("\nConnect a USB graphics adapter (VID 0x17e9) directly to the host port.")
    dut.expect("VENDOR_SURVEY", timeout=90)
    dut.expect("VENDOR_OPEN", timeout=30)
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=30) == b"[PASS]"
