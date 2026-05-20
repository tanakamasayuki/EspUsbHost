"""
Purpose:
    Verify that EspUsbHost correctly communicates with an FTDI VCP device
    at multiple baud rates with multiple data patterns, confirmed both via
    USB loopback and via a direct UART sniff on an ESP32-S3 GPIO.

Why manual:
    Requires a real FTDI device (VID 0x0403) and physical wiring of the
    VCP TX/RX line to an ESP32-S3 GPIO. ESP32 cannot emulate the
    vendor-specific VCP protocol.

Required hardware:
    - ESP32-S3 host board
    - FTDI device (FT232R, FT232H, FT2232, FT4232, etc.)
    - Wiring:
        VCP TX --+-- VCP RX             (loopback)
                 +-- ESP32-S3 GPIO4     (UART1 RX sniff)
        GND  --- ESP32-S3 GND

Setup:
    1. Wire VCP TX, VCP RX, and ESP32-S3 GPIO4 together; share GND.
    2. Connect the FTDI device to the host board's USB port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/vcp_ftdi/vcp_ftdi.py -v -s
"""

import pytest

CONFIG_PATTERNS = {
    "9600-8N1": ["ascii", "allbytes", "long"],
    "38400-8N1": ["ascii", "allbytes", "long"],
    "115200-8N1": ["ascii", "allbytes", "long"],
    "460800-8N1": ["ascii", "allbytes", "long"],
    "57600-7E1": ["ascii"],
    "57600-7O2": ["ascii"],
}


def test_loopback_with_sniff(dut):
    """
    Expected result (pass):  For each (config, pattern) the sketch prints
                             "CONFIG=<name> PATTERN=<name> OK" and finishes with
                             "[PASS]".
    Expected result (fail):  Any "CONFIG=... PATTERN=... FAIL ..." or
                             sketch prints "[FAIL]"/times out.
    """
    for config, patterns in CONFIG_PATTERNS.items():
        for pattern in patterns:
            dut.expect_exact(f"CONFIG={config} PATTERN={pattern} OK", timeout=30)
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=10) == b"[PASS]"
