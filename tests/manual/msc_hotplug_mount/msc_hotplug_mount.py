"""
Purpose:
    Verify that a FatFs/VFS MSC mount is cleaned up when the USB flash drive is
    disconnected, and that the same mount path can be used again after reconnect.

Why manual:
    Requires a person to physically unplug and reconnect a real USB flash drive
    while it is mounted.

Required hardware:
    - ESP32-S3 host board
    - USB flash drive

Setup:
    1. Connect the host board to the PC.
    2. Leave the USB flash drive disconnected, or reconnect it when prompted.
    3. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    4. Run: uv run --env-file .env pytest manual/msc_hotplug_mount/msc_hotplug_mount.py -v -s
"""


def test_msc_hotplug_mount(dut):
    """
    Expected result (pass):  The sketch mounts /usb, the operator unplugs the
                             flash drive, reconnects it, and /usb mounts again.
    Expected result (fail):  Remount fails, no disconnect event appears, crash,
                             or timeout.
    """
    dut.expect("MSC_HOTPLUG_READY")
    print("\nConnect the USB flash drive if it is not already connected.")
    dut.expect("MSC_HOTPLUG_MOUNT ok=1 path=/usb", timeout=45)

    print("\nUnplug the USB flash drive now.")
    dut.expect("MSC_HOTPLUG_DISCONNECT", timeout=60)
    dut.expect("MSC_HOTPLUG_DISCONNECT_CLEANUP observed=1", timeout=10)

    print("\nReconnect the same USB flash drive now.")
    dut.expect("MSC_HOTPLUG_REMOUNT ok=1 path=/usb", timeout=60)
    dut.expect("MSC_HOTPLUG_FINAL_UNMOUNT ok=1 path=/usb", timeout=10)
    dut.expect(r"\[PASS\]", timeout=10)
