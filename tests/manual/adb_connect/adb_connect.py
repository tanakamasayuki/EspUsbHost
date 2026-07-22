"""
Purpose:
    Verify that EspUsbHost authenticates a real Android ADB transport, opens a
    single shell stream, and completes an echo command without crashing.

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
    5. When prompted, connect and unlock the Android device. On the first run,
       approve the USB debugging dialog. The generated RSA key is kept in NVS,
       so a second run verifies authorization with the saved key.
"""


def test_adb_connect(dut):
    """
    Expected result (pass):  An ff/42/01 interface is claimed, ADB authentication
                             completes, shell:echo returns ESP_USB_HOST_ADB_OK,
                             and the sketch remains alive for two seconds.
    Expected result (fail):  Authentication, stream setup, shell output, bulk
                             transfer, protocol validation, or stability fails.
    """
    dut.expect("adb_connect test start")
    print("\nConnect and unlock an Android device with USB debugging enabled.")
    print("Approve the USB debugging dialog if it appears.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=140) == b"[PASS]"
