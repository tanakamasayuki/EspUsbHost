"""
Purpose:
    Verify the ALIENTEK DP100 write path: the BASIC_SET frame with its 0x20 index
    flag, the state byte as the output enable, and that the protection thresholds
    survive a setpoint change. Nothing is taken on trust from the write's answer -
    the DP100 reports success for a write it then ignores - so every step is
    confirmed by reading the setpoint back and by reading what the output is doing.

SAFETY:
    THIS ENERGISES THE SUPPLY'S OUTPUT TERMINALS at 5.000 V / 0.500 A. Run it only
    with nothing connected to them. The test refuses to start if the output is
    already on, switches it off again, and restores the original setpoint.
    The read-only checks are in manual/dp100 and are safe with a load connected.

Why manual:
    Requires a physical DP100 and a human who knows what is wired to it.

Required hardware:
    - ESP32-S3 host board
    - ALIENTEK DP100 connected directly to the host port, output terminals bare

Setup:
    1. Disconnect everything from the DP100's output terminals.
    2. Connect the host board to the PC and the DP100 to its USB host port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/dp100_output/dp100_output.py -v -s
"""


def test_dp100_output(dut):
    """
    Expected result (pass):  The setpoint write is read back as 5000 mV / 500 mA with
                             the output still off, switching the output on measures
                             about 5.000 V, switching it off measures 0, the original
                             setpoint is restored, and "[PASS]".
    Expected result (fail):  The output already on at the start, a write that does not
                             take, no voltage after enabling the output, a refused
                             frame, the thresholds not carried through, crash, or
                             timeout.
    """
    dut.expect("dp100_output test start")
    print("\nConnect an ALIENTEK DP100 with NOTHING on its output terminals.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"
