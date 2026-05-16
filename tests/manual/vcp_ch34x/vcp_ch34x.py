"""
Purpose:
    Verify that EspUsbHost correctly communicates with a CH34x VCP device
    at multiple baud rates, confirmed both via USB loopback and via a
    direct UART sniff on an ESP32-S3 GPIO.

Why manual:
    Requires a real CH34x device (VID 0x1A86) and physical wiring of the
    VCP TX/RX line to an ESP32-S3 GPIO. ESP32 cannot emulate the
    vendor-specific VCP protocol.

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
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/vcp_ch34x/vcp_ch34x.py -v -s
"""

import pytest

BAUDS = [9600, 38400, 115200, 460800]


def test_loopback_with_sniff(dut):
    """
    Expected result (pass):  Each baud rate prints "BAUD=<n> OK" and the
                             sketch finishes with "[PASS]".
    Expected result (fail):  Any baud prints "BAUD=<n> FAIL ..." or the
                             sketch prints "[FAIL]"/times out.
    """
    for baud in BAUDS:
        dut.expect_exact(f"BAUD={baud} OK", timeout=30)
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=10) == b"[PASS]"
