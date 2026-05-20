"""
Purpose:
    Verify that EspUsbHost can fetch and print HID report descriptors from
    a connected USB HID device.

Required hardware:
    - ESP32-S3 host board
    - USB HID keyboard or mouse connected to the host board

Setup:
    1. Connect the HID device to the host board's USB port.
    2. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    3. Run: uv run --env-file .env pytest manual/hid_report_descriptor/hid_report_descriptor.py -v -s
"""


def test_hid_report_descriptor(dut):
    dut.expect("HID report descriptor test start")
    dut.expect("HID report descriptor:", timeout=20)
    dut.expect(r"\[PASS\]")
