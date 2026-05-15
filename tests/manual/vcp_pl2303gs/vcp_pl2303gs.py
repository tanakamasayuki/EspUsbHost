"""
Purpose:
    Verify that EspUsbHost correctly communicates with a PL2303GS VCP device
    using TX/RX loopback.

Why manual:
    Requires a real PL2303GS device (VID 0x067B, PID 0x23A3). ESP32 cannot emulate this
    vendor-specific VCP protocol.

Required hardware:
    - ESP32-S3 host board
    - PL2303GS device (067B:23A3) with TX and RX pins shorted

Setup:
    1. Short TX and RX on the PL2303GS device.
    2. Connect the PL2303GS device to the host board's USB port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/vcp_pl2303gs/vcp_pl2303gs.py -v -s
"""

import pytest


def test_loopback(dut):
    """
    Expected result (pass):  Sketch prints "[PASS]" after loopback data matches.
    Expected result (fail):  "[FAIL]" with received byte count, or timeout.
    """
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=30) == b"[PASS]"
