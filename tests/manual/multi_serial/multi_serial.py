"""
Purpose:
    Verify that EspUsbHost handles two USB serial devices simultaneously,
    with each assigned to a separate EspUsbHostCdcSerial instance via setAddress().

Why manual:
    Requires two physical USB serial devices (CDC ACM or VCP) connected at
    the same time with TX/RX shorted on each.

Required hardware:
    - ESP32-S3 host board with a USB hub or two USB ports
    - Two USB serial devices (e.g., two FTDI dongles, or FTDI + CDC ACM)
      with TX and RX pins shorted on each

Setup:
    1. Short TX and RX on both serial devices.
    2. Connect both devices to the host board's USB port(s).
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/multi_serial/multi_serial.py -v -s
"""

import pytest


def test_two_serial_devices(dut):
    """
    Expected result (pass):  Both devices are assigned, loopback succeeds on
                             both, sketch prints "[PASS]".
    Expected result (fail):  Only one device assigned, loopback fails on either
                             device, or timeout.
    """
    dut.expect("Both devices assigned", timeout=30)
    dut.expect(r"\[PASS\]", timeout=30)
