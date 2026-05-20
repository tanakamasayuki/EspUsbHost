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

CONFIG_PATTERNS = {
    "300-8N1": ["ascii"],
    "1200-8N1": ["ascii", "allbytes", "long"],
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
