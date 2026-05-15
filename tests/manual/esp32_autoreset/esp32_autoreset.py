"""
Purpose:
    Verify that DTR/RTS control from a USB serial adapter can reset a target
    ESP32 and put it into ROM download mode.

Why manual:
    Requires a physical USB serial adapter wired to a target ESP32's UART0,
    EN, and GPIO0 auto-reset circuit. Wiring and logic polarity are board
    dependent.

Required hardware:
    - ESP32-S3 host board
    - USB serial adapter under test
    - Target ESP32 board or module
    - TX/RX connected between the USB serial adapter and target ESP32 UART0
    - DTR/RTS connected through a typical ESP32 auto-reset circuit

Setup:
    1. Connect the USB serial adapter to the host board's USB port.
    2. Wire the adapter to the target ESP32 UART0 and auto-reset circuit.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/esp32_autoreset/esp32_autoreset.py -v -s
"""


def test_esp32_autoreset(dut):
    """
    Expected result (pass):  Sketch prints "[PASS]" after detecting both a
                             normal boot log and a download-mode boot log.
    Expected result (fail):  Sketch prints "[FAIL]" or times out.
    """
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=40) == b"[PASS]"
