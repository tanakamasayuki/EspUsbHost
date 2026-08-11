"""
Purpose:
    Verify the USBTMC message layer against a real instrument: the class 0xfe /
    subclass 0x03 interface is found and claimed through the vendor bulk API, the
    class requests (GET_CAPABILITIES, CLEAR) run on EP0 via
    vendorControlTransfer(), and SCPI commands and queries are exchanged over the
    bulk endpoints.

Why manual:
    Requires a physical USBTMC instrument. No peer device implements USBTMC, so
    the protocol cannot be exercised against the sibling EspUsbDevice library.

Required hardware:
    - ESP32-S3 host board
    - KIKUSUI PMX series DC power supply (developed against a PMX18-5A, 0b3e:1029)

Safety:
    The test never switches the output on. It writes voltage and current
    *settings*, reads them back, and reads measurements with the output off.

Setup:
    1. Connect the host board to the PC.
    2. Connect the power supply to the host board's USB host port and switch it on.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/usbtmc_scpi/usbtmc_scpi.py -v -s
"""


def test_usbtmc_scpi(dut):
    """
    Expected result (pass):  Sketch prints the capabilities, an *IDN? naming
                             KIKUSUI, a matching setting readback, measurements,
                             an empty error queue, and "[PASS]".
    Expected result (fail):  Instrument not found, a class request or query
                             failing, a readback mismatch, a non-empty error
                             queue, crash, or timeout.
    """
    dut.expect("usbtmc_scpi test start")
    print("\nConnect a KIKUSUI PMX series power supply.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"
