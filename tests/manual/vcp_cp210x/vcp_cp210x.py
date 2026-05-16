"""
Purpose:
    Verify that EspUsbHost correctly communicates with a CP210x VCP device
    at multiple baud rates with multiple data patterns, confirmed both via
    USB loopback and via a direct UART sniff on an ESP32-S3 GPIO.

Why manual:
    Requires a real CP210x device (VID 0x10C4) and physical wiring of the
    VCP TX/RX line to an ESP32-S3 GPIO.

Required hardware:
    - ESP32-S3 host board
    - CP210x device (CP2102, CP2104, etc.)
    - Wiring:
        VCP TX --+-- VCP RX             (loopback)
                 +-- ESP32-S3 GPIO4     (UART1 RX sniff)
        GND  --- ESP32-S3 GND

Setup:
    1. Wire VCP TX, VCP RX, and ESP32-S3 GPIO4 together; share GND.
    2. Connect the CP210x device to the host board's USB port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env.
    4. Run: uv run --env-file .env pytest manual/vcp_cp210x/vcp_cp210x.py -v -s
"""

import pytest

BAUDS = [9600, 38400, 115200, 460800]
PATTERNS = ["ascii", "allbytes", "long"]


def test_loopback_with_sniff(dut):
    for baud in BAUDS:
        for pattern in PATTERNS:
            dut.expect_exact(f"BAUD={baud} PATTERN={pattern} OK", timeout=30)
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=10) == b"[PASS]"
