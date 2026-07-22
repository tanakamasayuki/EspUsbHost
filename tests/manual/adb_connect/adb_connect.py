"""
Purpose:
    Verify that EspUsbHost finds a real Android ADB interface, sends A_CNXN,
    and receives the device's first valid ADB response without crashing.

Why manual:
    Requires a physical Android device with USB debugging enabled. Real Android
    USB descriptors, ADB transport timing, and authorization state cannot be
    fully reproduced by the automated peer device.

Required hardware:
    - ESP32-S3 host board
    - Android device with Developer options and USB debugging enabled
    - USB data cable (a USB hub may be used)

Setup:
    1. Connect the host board to the PC, but leave the Android device unplugged.
    2. Enable USB debugging on the Android device.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/adb_connect/adb_connect.py -v -s
    5. When prompted, connect and unlock the Android device. Either an AUTH
       challenge or an already-authorized CNXN response is accepted.
"""


def test_adb_connect(dut):
    """
    Expected result (pass):  An ff/42/01 interface is claimed, A_CNXN is sent,
                             and a valid AUTH or CNXN message is received. The
                             sketch remains alive for two seconds afterward.
    Expected result (fail):  No ADB interface/response, an invalid ADB message,
                             a bulk OUT failure, crash, or timeout.
    """
    dut.expect("adb_connect test start")
    print("\nConnect and unlock an Android device with USB debugging enabled.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=40) == b"[PASS]"
