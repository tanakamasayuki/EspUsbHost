"""
Purpose:
    Verify the FeliCa IDm path of examples/Ccid/EspUsbHostCcidFelicaIdm against a
    real Sony RC-S300: the transparent session (start session, switch protocol to
    FeliCa, RF on/off), a FeliCa Polling with an explicit System Code, and the IDm
    out of the answer. The reader's own polling is logged alongside it, so the log
    shows both what the reader finds on its own and what a chosen System Code
    finds.

Why manual:
    Requires a physical RC-S300 with a FeliCa card on it, and the interesting case
    (a phone whose wallet answers the wildcard as something other than the transit
    card) cannot be produced in software at all.

Required hardware:
    - ESP32-S3 host board
    - Sony RC-S300 (`FeliCa Port/PaSoRi 4.0`, VID 0x054c PID 0x0dc8)
    - A FeliCa card. A transit card (Suica, PASMO, ...) exercises System Code
      0x0003; any other FeliCa card exercises the wildcard path and the filter
      rejecting 0x0003.

Setup:
    1. Connect the host board to the PC.
    2. Connect the RC-S300 to the host board's USB host port.
    3. Have the FeliCa card ready; the sketch waits up to 30 s for one.
    4. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    5. Run: uv run --env-file .env pytest manual/ccid_felica/ccid_felica.py -v -s

Notes:
    An iPhone is worth a second run: hold it on the reader instead of the card and
    compare FELICA_WILDCARD with FELICA_TRANSIT. A wallet that answers the
    wildcard with its own card and 0x0003 with the Suica is the whole reason this
    path exists, and it shows up as FELICA_COMPARE reporting different IDm.
"""


def test_ccid_felica(dut):
    """
    Expected result (pass):  RCS300_OPEN, RCS300_SESSION ok, RCS300_SWITCH ok
                             (protocol=0x00: the reader names no protocol when it
                             accepts the switch), RCS300_RF ok, a FELICA_WILDCARD line
                             carrying an 8-byte IDm, a stable IDm on FELICA_REPEAT,
                             and "[PASS]". A card with no transit system passes
                             with FELICA_TRANSIT reporting no answer -- that is the
                             System Code filtering.
    Expected result (fail):  No RC-S300, a refused transparent session command, no
                             FeliCa target answering the wildcard within 30 s, or
                             an IDm that changes between polls.
    """
    dut.expect("ccid_felica test start")
    print("\nPlace a FeliCa card (Suica etc.) on the reader and keep it there.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=120) == b"[PASS]"
