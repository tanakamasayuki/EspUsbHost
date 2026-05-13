"""
Purpose:
    Verify that EspUsbHost correctly sends NumLock, CapsLock, and ScrollLock
    LED commands to a USB HID keyboard.

Why manual:
    Pass/fail depends on whether a physical LED lights up — this cannot be
    observed in software.

Required hardware:
    - ESP32-S3 host board
    - USB HID keyboard with NumLock, CapsLock, and ScrollLock indicator LEDs

Setup:
    1. Connect the keyboard to the host board's USB port.
    2. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    3. Run: uv run --env-file .env pytest manual/keyboard_leds/keyboard_leds.py -v -s
"""

import pytest


def _confirm(dut, prompt: str) -> None:
    """Ask the operator to confirm the LED state, then advance the sketch."""
    answer = input(f"{prompt} [y/n] > ").strip().lower()
    if answer != "y":
        pytest.fail(f"Operator reported FAIL: {prompt}")
    dut.write(b"\n")


def test_numlock_led(dut):
    """
    Expected result (pass):  NumLock LED turns ON and OFF on operator confirmation.
    Expected result (fail):  LED does not change state.
    """
    dut.expect("NumLock ON")
    _confirm(dut, "NumLock LED ON?")
    dut.expect("NumLock OFF")
    _confirm(dut, "NumLock LED OFF?")


def test_capslock_led(dut):
    """
    Expected result (pass):  CapsLock LED turns ON and OFF on operator confirmation.
    Expected result (fail):  LED does not change state.
    """
    dut.expect("CapsLock ON")
    _confirm(dut, "CapsLock LED ON?")
    dut.expect("CapsLock OFF")
    _confirm(dut, "CapsLock LED OFF?")


def test_scrolllock_led(dut):
    """
    Expected result (pass):  ScrollLock LED turns ON and OFF on operator confirmation.
    Expected result (fail):  LED does not change state.
    """
    dut.expect("ScrollLock ON")
    _confirm(dut, "ScrollLock LED ON?")
    dut.expect("ScrollLock OFF")
    _confirm(dut, "ScrollLock LED OFF?")
