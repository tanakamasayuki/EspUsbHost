"""
Purpose:
    Verify that EspUsbHost correctly communicates with a CH34x VCP device
    using TX/RX loopback.

Why manual:
    Requires a real CH34x device (VID 0x1A86). ESP32 cannot emulate this
    vendor-specific VCP protocol.

Required hardware:
    - ESP32-S3 host board
    - CH34x device (CH340, CH341, etc.) with TX and RX pins shorted

Setup:
    1. Short TX and RX on the CH34x device.
    2. Connect the CH34x device to the host board's USB port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/vcp_ch34x/vcp_ch34x.py -v -s
"""

import pytest


def test_loopback(dut):
    """
    Expected result (pass):  Sketch prints "[PASS]" after loopback data matches.
    Expected result (fail):  "[FAIL]" with received byte count, or timeout.
    """
    dut.expect(r"\[PASS\]", timeout=30)
