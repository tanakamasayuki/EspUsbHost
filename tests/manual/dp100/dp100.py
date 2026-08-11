"""
Purpose:
    Verify the ALIENTEK DP100 HID protocol against real hardware: the framed
    request/response exchange over onHIDInput() and sendHIDVendorOutput(), the
    DEVICE_INFO and BASIC_INFO field offsets, the mV / 0.1 degC scales, and that
    repeated and interleaved reads stay paired with their requests.

Why manual:
    Requires a physical DP100. No peer device implements its protocol, so it
    cannot be exercised against the sibling EspUsbDevice library.

Required hardware:
    - ESP32-S3 host board
    - ALIENTEK DP100 (ATK-MDP100, 2e3c:af01) connected directly to the host port

Safety:
    Read only. The setpoint frame (BASIC_SET) carries the output enable and its
    request form is not confirmed, so this test never sends it. Safe to run with a
    load connected.

Setup:
    1. Connect the host board to the PC.
    2. Connect the DP100 to the host board's USB host port and power it.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/dp100/dp100.py -v -s
"""


def test_dp100(dut):
    """
    Expected result (pass):  Sketch prints the device info, plausible BASIC_INFO
                             values, 50 repeated and 5 interleaved reads with no
                             refusals, and "[PASS]".
    Expected result (fail):  Device not found, a read failing, a value outside its
                             physical range (which means a wrong offset or scale),
                             a refused frame, crash, or timeout.
    """
    dut.expect("dp100 test start")
    print("\nConnect an ALIENTEK DP100 directly to the USB host port.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"
