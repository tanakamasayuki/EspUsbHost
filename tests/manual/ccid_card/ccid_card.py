"""
Purpose:
    Verify the CCID API end to end against a real reader: ccidOpen(),
    ccidGetInterface(), ccidGetStatus(), ccidPowerOn() (ATR) and ccidApdu()
    with the PC/SC Get UID pseudo APDU (FF CA 00 00 00).

Why manual:
    Requires a physical CCID smart card reader with a card on / in it.

Required hardware:
    - ESP32-S3 host board
    - CCID smart card reader (e.g. Sony RC-S300 PaSoRi)
    - An ISO 14443 card placed on the reader (or a card inserted in the slot)

Setup:
    1. Connect the host board to the PC.
    2. Connect the CCID reader to the host board's USB host port.
    3. Have a card ready; the sketch waits up to 30 s for one to be placed.
    4. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    5. Run: uv run --env-file .env pytest manual/ccid_card/ccid_card.py -v -s
"""


def test_ccid_card(dut):
    """
    Expected result (pass):  Sketch prints CCID_INTERFACE, CCID_STATUS with
                             present=1, a non-empty CCID_ATR, CCID_APDU with
                             sw=9000, and "[PASS]".
    Expected result (fail):  Open/status/power-on/APDU failure, no card, or a
                             status word other than 9000.
    """
    dut.expect("ccid_card test start")
    print("\nPlace a card on the reader and keep it there (a phone in Apple Pay / FeliCa mode works).")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=90) == b"[PASS]"
