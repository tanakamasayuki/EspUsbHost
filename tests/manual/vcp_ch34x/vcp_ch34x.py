"""
Purpose:
    Verify that EspUsbHost correctly communicates with a CH34x VCP device
    at multiple baud rates with multiple data patterns, confirmed both via
    USB loopback and via a direct UART sniff on an ESP32-S3 GPIO.

Why manual:
    Requires a real CH34x device (VID 0x1A86) and physical wiring of the
    VCP TX/RX line to an ESP32-S3 GPIO.

Required hardware:
    - ESP32-S3 host board
    - CH34x device (CH340, CH341, etc.)
    - Wiring:
        VCP TX --+-- VCP RX             (loopback)
                 +-- ESP32-S3 GPIO4     (UART1 RX sniff)
        GND  --- ESP32-S3 GND

Setup:
    1. Wire VCP TX, VCP RX, and ESP32-S3 GPIO4 together; share GND.
    2. Connect the CH34x device to the host board's USB port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env.
    4. Run: uv run --env-file .env pytest manual/vcp_ch34x/vcp_ch34x.py -v -s
"""

import pytest

BAUDS = [9600, 38400, 115200, 460800]
PATTERNS = ["ascii", "allbytes", "long"]


def test_loopback_with_sniff(dut):
    for baud in BAUDS:
        for pattern in PATTERNS:
            dut.expect_exact(f"BAUD={baud} PATTERN={pattern} OK", timeout=30)
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=10) == b"[PASS]"
