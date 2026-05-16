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

BAUDS = [9600, 38400, 115200, 460800]
PATTERNS = ["ascii", "allbytes", "long"]


def test_loopback_with_sniff(dut):
    """
    Expected result (pass):  For each (baud, pattern) the sketch prints
                             "BAUD=<n> PATTERN=<name> OK" and finishes with
                             "[PASS]".
    Expected result (fail):  Any "BAUD=... PATTERN=... FAIL ..." or
                             sketch prints "[FAIL]"/times out.
    """
    for baud in BAUDS:
        for pattern in PATTERNS:
            dut.expect_exact(f"BAUD={baud} PATTERN={pattern} OK", timeout=30)
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=10) == b"[PASS]"
