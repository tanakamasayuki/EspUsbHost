"""
Purpose:
    Dump the interface and endpoint layout of the connected USB devices and
    report whether a CCID interface (bInterfaceClass == 0x0b) is present.
    Used to ground the CCID API design against real reader hardware.

Why manual:
    Requires a physical CCID smart card reader (e.g. Sony RC-S300 PaSoRi).

Required hardware:
    - ESP32-S3 host board
    - CCID smart card reader connected to the host board's USB host port
    - An ICC placed on / inserted into the reader

Setup:
    1. Connect the host board to the PC.
    2. Connect the CCID reader to the host board's USB host port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/ccid_info/ccid_info.py -v -s
"""


def test_ccid_info(dut):
    """
    Expected result (pass):  Sketch prints DEVICE / INTERFACE / ENDPOINT lines
                             and "[PASS]" when a CCID interface is found.
    Expected result (fail):  No CCID interface in the dump.
    """
    dut.expect("ccid_info test start")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=40) == b"[PASS]"
