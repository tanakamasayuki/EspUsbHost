"""
Purpose:
    Verify that EspUsbHost correctly sends NumLock and CapsLock LED commands
    to a USB HID keyboard.

Why manual:
    Requires a physical HID keyboard with working indicator LEDs.
    LED state is only observable by physically watching the keyboard.

Required hardware:
    - ESP32-S3 host board flashed with the EspUsbHostHIDKeyboard sketch
    - USB HID keyboard with NumLock / CapsLock indicator LEDs

Setup:
    1. Flash the host board with the EspUsbHostHIDKeyboard sketch.
    2. Connect the keyboard to the host board's USB port.
    3. Run: uv run --env-file .env pytest manual/keyboard_leds/keyboard_leds.py -v -s
"""

import pytest


def test_numlock_led():
    """
    Expected result (pass):  NumLock LED toggles on/off as the sketch sends LED commands.
    Expected result (fail):  LED does not change state.
    """
    print("\nThe sketch toggles the NumLock LED every 2 seconds.")
    answer = input("Does the NumLock LED toggle on and off? [y/n] > ").strip().lower()
    if answer != "y":
        pytest.fail("NumLock LED did not toggle")


def test_capslock_led():
    """
    Expected result (pass):  CapsLock LED toggles on/off as the sketch sends LED commands.
    Expected result (fail):  LED does not change state.
    """
    print("\nThe sketch toggles the CapsLock LED every 2 seconds.")
    answer = input("Does the CapsLock LED toggle on and off? [y/n] > ").strip().lower()
    if answer != "y":
        pytest.fail("CapsLock LED did not toggle")
