"""
Purpose:
    Verify that EspUsbHost delivers events independently from a USB keyboard
    and a USB mouse connected simultaneously.

Why manual:
    Requires two physical USB HID devices connected at the same time.

Required hardware:
    - ESP32-S3 host board with a USB hub or two USB ports
    - USB keyboard
    - USB mouse

Setup:
    1. Connect both devices to the host board's USB port(s).
    2. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    3. Run: uv run --env-file .env pytest manual/multi_hid_keyboard_mouse/multi_hid_keyboard_mouse.py -v -s
"""

import pytest


def test_keyboard_and_mouse(dut):
    """
    Expected result (pass):  Sketch receives at least one event from the keyboard
                             and one from the mouse, then prints "[PASS]".
    Expected result (fail):  Only one device delivers events, or timeout.
    """
    dut.expect("Press a key and move the mouse")
    print("\nPress a key on the keyboard AND move the mouse.")
    dut.expect(r"\[PASS\]", timeout=60)
