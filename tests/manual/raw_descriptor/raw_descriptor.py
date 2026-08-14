"""
Purpose:
    Read the raw DEVICE and CONFIGURATION descriptors of every enumerated device
    with standard GET_DESCRIPTOR requests on EP0, and walk the configuration
    block by block, printing the bytes of each block with its bDescriptorType.

    device_dump prints what the library parsed; this prints the bytes themselves,
    including the class-specific descriptors the parsed dump never shows (HID,
    CDC functional, CCID, UAC). Those bytes are what a USBPcap capture or a
    `lsusb -v` output on a PC has to be compared against when working out how an
    unsupported device is put together.

Why manual:
    The device under inspection is whatever the operator plugged in, so there is
    nothing to assert beyond "the descriptors were readable".

Known limit:
    ESP-IDF caps a control transfer at 256 bytes including the 8-byte setup
    packet, so at most 248 descriptor bytes are read. A longer configuration is
    reported as truncated; a device whose configuration descriptor exceeds 256
    bytes in total cannot enumerate on this stack at all.

Required hardware:
    - ESP32-S3 host board (or ESP32-P4 with --profile esp32p4)
    - Any USB device connected to the host port

Setup:
    1. Connect the host board to the PC.
    2. Connect the device to inspect to the host board's USB host port.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/raw_descriptor/raw_descriptor.py -v -s
"""


def test_raw_descriptor(dut):
    """
    Expected result (pass):  Sketch prints the raw descriptors, the per-block
                             walk, and "[PASS]".
    Expected result (fail):  No device enumerated, a GET_DESCRIPTOR failure,
                             crash, or timeout.
    """
    dut.expect("raw_descriptor test start")
    print("\nConnect the USB device to inspect.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=40) == b"[PASS]"
