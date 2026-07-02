"""
Purpose:
    Verify that a real USB Ethernet adapter exposes a complete CDC-ECM or
    CDC-NCM descriptor candidate that EspUsbHost can identify generically.

Why manual:
    Requires a physical USB Ethernet adapter. Descriptor layouts and supported
    configurations vary between real NIC chipsets and products.

Required hardware:
    - ESP32-S3 host board
    - USB Ethernet adapter with CDC-ECM or CDC-NCM support

Setup:
    1. Connect the host board to the PC.
    2. Connect the USB Ethernet adapter to the host board's USB host port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/usb_network_descriptor/usb_network_descriptor.py -v -s
"""


def test_usb_network_descriptor(dut):
    """
    Expected result (pass):  At least one complete CDC-ECM or CDC-NCM candidate
                             is printed with control interface, data interface,
                             bulk IN endpoint, and bulk OUT endpoint.
    Expected result (fail):  No complete network candidate is found, crash, or
                             timeout.
    """
    dut.expect("usb_network_descriptor test start")
    print("\nConnect a USB Ethernet adapter with CDC-ECM or CDC-NCM support.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=30) == b"[PASS]"
