"""
Purpose:
    Verify that EspUsbHost correctly communicates with a PL2303 VCP device
    at multiple baud rates with multiple data patterns, confirmed both via
    USB loopback and via a direct UART sniff on an ESP32-S3 GPIO.

Why manual:
    Requires a real PL2303 device (VID 0x067B, PID 0x2303) and physical
    wiring of the VCP TX/RX line to an ESP32-S3 GPIO.

Required hardware:
    - ESP32-S3 host board
    - PL2303 device (067B:2303)
    - Wiring:
        VCP TX --+-- VCP RX             (loopback)
                 +-- ESP32-S3 GPIO4     (UART1 RX sniff)
        GND  --- ESP32-S3 GND

Setup:
    1. Wire VCP TX, VCP RX, and ESP32-S3 GPIO4 together; share GND.
    2. Connect the PL2303 device to the host board's USB port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env.
    4. Run: uv run --env-file .env pytest manual/vcp_pl2303/vcp_pl2303.py -v -s
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
    for config, patterns in CONFIG_PATTERNS.items():
        for pattern in patterns:
            dut.expect_exact(f"CONFIG={config} PATTERN={pattern} OK", timeout=30)
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=10) == b"[PASS]"
