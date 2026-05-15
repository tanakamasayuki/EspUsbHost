"""
Purpose:
    Verify that a USB hub and a downstream device are detected and that the
    downstream device exposes topology information.

Why manual:
    Requires a physical USB hub and a physical USB device connected through it.
    The hub topology is the behavior under test, so automated direct USB
    connections are not sufficient.

Required hardware:
    - ESP32-S3 host board
    - USB hub
    - Two USB devices connected to downstream ports of the hub

Setup:
    1. Connect the host board to the PC.
    2. Connect a USB hub to the host board's USB host port.
    3. Connect two USB devices to the hub.
    4. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    5. Run: uv run --env-file .env pytest manual/hub_info/hub_info.py -v -s
"""


def test_hub_info(dut):
    """
    Expected result (pass):  Sketch prints the topology and "[PASS]" after
                             detecting either explicit hub/child topology or
                             multiple root-port IDs reported by the USB host
                             stack for devices behind the hub.
    Expected result (fail):  Hub or child topology info is missing, crash, or
                             timeout.
    """
    dut.expect("hub_info test start")
    print("\nConnect a USB hub with at least two downstream USB devices.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=30) == b"[PASS]"
