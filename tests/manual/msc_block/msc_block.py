"""
Purpose:
    Verify that a real USB Mass Storage device can be enumerated, queried for
    capacity, and read at LBA 0 without writing to the device.

Why manual:
    Requires a physical USB storage device. Peer tests cover the protocol
    skeleton, but real USB flash drives vary in descriptors, timing, and SCSI
    command behavior.

Required hardware:
    - ESP32-S3 or ESP32-P4 host board
    - USB flash drive connected to the host port

Setup:
    1. Connect the board to the host PC.
    2. Connect a USB flash drive to the board's USB host port.
    3. Set TEST_SERIAL_PORT_ESP32S3 or TEST_SERIAL_PORT_ESP32P4 in .env.
    4. Run: uv run --env-file .env pytest manual/msc_block/msc_block.py -v -s
"""


def test_msc_block_read(dut):
    """
    Expected result (pass):  The sketch prints capacity, reads LBA 0, sees the
                             0x55AA signature, and prints "[PASS]".
    Expected result (fail):  No MSC device, capacity/read failure, missing
                             boot-sector signature, crash, or timeout.
    """
    dut.expect("MSC_MANUAL_READY")
    dut.expect("MSC_DEVICE", timeout=30)
    dut.expect("MSC_INQUIRY")
    dut.expect("MSC_TEST_UNIT_READY ok=1")
    dut.expect("MSC_WAIT_READY ok=1")
    dut.expect("MSC_MAX_LUN")
    dut.expect("MSC_SELECT_LUN lun=0 ok=1")
    dut.expect("MSC_CAPACITY blocks=")
    dut.expect("MSC_BLOCK_DEVICE")
    dut.expect("MSC_CAPACITY64 blocks=")
    dut.expect("MSC_LBA0_64")
    dut.expect("MSC_SYNC_CACHE")
    dut.expect("MSC_LBA0")
    dut.expect(r"\[PASS\]", timeout=30)
