"""
Purpose:
    Verify that the manual test infrastructure works end-to-end:
    sketch build/flash, serial communication, and operator confirmation.

Why manual:
    This is a procedure check, not a feature test.
    Run it once to confirm the manual test workflow before writing real tests.

Required hardware:
    - ESP32-S3 or ESP32-P4 board

Setup:
    1. Connect the board to the host PC.
    2. Set TEST_SERIAL_PORT_ESP32S3 (or TEST_SERIAL_PORT_ESP32P4) in .env.
    3. Run: uv run --env-file .env pytest manual/smoke/smoke.py -v -s
"""

import pytest


def test_serial_output(dut):
    """
    Expected result (pass):  Board prints "SMOKE ready" within timeout.
    Expected result (fail):  Serial output does not appear (board not flashed or wrong port).
    """
    dut.expect("SMOKE ready")


def test_operator_confirm(dut):
    """
    Expected result (pass):  Operator can see serial output lines in the terminal.
    Expected result (fail):  Operator reports output is not visible.
    """
    print("\nSerial output from the board should be visible above.")
    answer = input("Is the serial output visible in the terminal? [y/n] > ").strip().lower()
    if answer != "y":
        pytest.fail("Operator reported serial output not visible")
