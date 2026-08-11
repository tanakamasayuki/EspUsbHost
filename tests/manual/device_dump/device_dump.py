"""
Purpose:
    Dump the full descriptor state of every enumerated USB device: device
    descriptor, configuration, interfaces, endpoints and channel usage, plus a
    note for interfaces this library does not claim by itself (USBTMC, printer,
    vendor-specific) listing the bulk/interrupt endpoints a wrapper would use.

Why manual:
    The device under inspection is whatever the operator plugged in, so there is
    nothing to assert beyond "descriptors were readable".

Required hardware:
    - ESP32-S3 host board
    - Any USB device connected to the host port (directly or through a hub)

Setup:
    1. Connect the host board to the PC.
    2. Connect the device to inspect to the host board's USB host port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/device_dump/device_dump.py -v -s
"""


def test_device_dump(dut):
    """
    Expected result (pass):  Sketch prints the descriptor dump and "[PASS]".
    Expected result (fail):  No device enumerated, crash, or timeout.
    """
    dut.expect("device_dump test start")
    print("\nConnect the USB device to inspect.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=40) == b"[PASS]"
