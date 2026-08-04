"""
Purpose:
    Verify that CCID slot-change notifications from the reader's interrupt IN
    endpoint reach onCcidCardRemoved() and onCcidCardInserted().

Why manual:
    Requires a physical CCID reader and an operator to remove and re-place the
    card while the test runs.

Required hardware:
    - ESP32-S3 host board
    - CCID smart card reader with an interrupt IN endpoint
    - A card, placed on the reader before the run

Setup:
    1. Connect the host board to the PC.
    2. Connect the CCID reader to the host board's USB host port.
    3. Place a card on the reader.
    4. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    5. Run: uv run --env-file .env pytest manual/ccid_hotplug/ccid_hotplug.py -v -s
    6. When prompted, lift the card off the reader and put it back.
"""


def test_ccid_hotplug(dut):
    """
    Expected result (pass):  Sketch prints CCID_REMOVED, then CCID_INSERTED,
                             then "[PASS]".
    Expected result (fail):  Either notification missing within 60 s, the reader
                             has no interrupt IN endpoint, or ccidOpen() fails.
    """
    dut.expect("ccid_hotplug test start")
    print("\nRemove the card from the reader, then put it back.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=90) == b"[PASS]"
