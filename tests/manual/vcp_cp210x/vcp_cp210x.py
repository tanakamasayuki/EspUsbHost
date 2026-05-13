"""
Purpose:
    Verify that EspUsbHost correctly communicates with a CP210x VCP device
    using TX/RX loopback.

Why manual:
    Requires a real CP210x device (VID 0x10C4). ESP32 cannot emulate this
    vendor-specific VCP protocol.

Required hardware:
    - ESP32-S3 host board
    - CP210x device (CP2102, CP2104, etc.) with TX and RX pins shorted

Setup:
    1. Short TX and RX on the CP210x device.
    2. Connect the CP210x device to the host board's USB port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/vcp_cp210x/vcp_cp210x.py -v -s
"""

import pytest


def test_loopback(dut):
    """
    Expected result (pass):  Sketch prints "[PASS]" after loopback data matches.
    Expected result (fail):  "[FAIL]" with received byte count, or timeout.
    """
    dut.expect(r"\[PASS\]", timeout=30)
