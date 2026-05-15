"""
Purpose:
    Verify that per-port power control works on a USB hub by turning one
    downstream port off and back on.

Why manual:
    Requires a physical USB hub that supports per-port power switching and a
    physical downstream USB device.

Required hardware:
    - ESP32-S3 host board
    - USB hub with per-port power switching support
    - Any USB device connected to one downstream port of the hub

Setup:
    1. Connect the host board to the PC.
    2. Connect the USB hub to the host board's USB host port.
    3. Connect a USB device to one downstream port of the hub.
    4. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    5. Run: uv run --env-file .env pytest manual/hub_power/hub_power.py -v -s
"""


def test_hub_power(dut):
    """
    Expected result (pass):  Sketch powers off the selected hub port, observes
                             disconnect, powers the port back on, observes
                             reconnect, and prints "[PASS]".
    Expected result (fail):  Power request fails, disconnect/reconnect is not
                             observed, crash, or timeout.
    """
    dut.expect("hub_power test start")
    print("\nConnect a USB hub with at least one downstream USB device.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=40) == b"[PASS]"
