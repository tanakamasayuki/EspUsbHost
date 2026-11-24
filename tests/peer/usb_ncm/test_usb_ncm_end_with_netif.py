"""end() with the USB NIC's netif still attached, then the host restarts.

end() has to take the netif down with the device. Where end() unpowers the root
port first (ESP-IDF 6 builds), the device leaves through DEV_GONE, whose path
stops the netif. Where it leaves an occupied port powered (ESP32-S2 on
arduino-esp32 3.3.x), the device is closed by the client teardown instead,
which did not: the "USB_NCM" netif stayed registered in esp_netif, so the next
attach failed with "duplicate key" and the netif and its driver leaked. Seen in
the full run as peer/usb_ncm's next attach failing on the S2 host only.
"""
import time

from pexpect.exceptions import TIMEOUT

import pytest


def _poll_state(dut, pattern, attempts=100):
    for _ in range(attempts):
        dut.write("Q")
        try:
            return dut.expect(pattern, timeout=2)
        except TIMEOUT:
            continue
    raise AssertionError(f"the host never reported {pattern!r}")


def _wait_device_link(device, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        device.write("?")
        try:
            device.expect(r"DEVICE_READY ip=192\.168\.7\.1 link=1", timeout=1)
            return
        except TIMEOUT:
            continue
    raise AssertionError("the NCM peer never reported its link up")


@pytest.fixture(autouse=True)
def usb_host(dut, peers):
    """Start the host after the peer is flashed, and stop it however the test ends.

    See test_usb_ncm.py for why the host is gated and why `peers` is requested.
    """
    _poll_state(dut, r"HOST_STATE (?:idle|running) devices=\d+")
    dut.write("G")
    _poll_state(dut, r"HOST_STATE running devices=[1-9]")
    yield
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")


def _attach_and_use_network(dut, device):
    _wait_device_link(device)
    dut.write("a")
    dut.expect_exact("NETWORK_ATTACH ok=1")
    dut.expect(r"ip=192\.168\.7\.\d", timeout=30)
    dut.write("g")
    dut.expect_exact("HTTP_GET code=200 body=ESPUSB_NCM_OK")


def test_usb_ncm_end_with_netif_attached_releases_the_netif(dut, peers):
    """
    Expected result (pass):  after end() no "USB_NCM" netif is left in esp_netif,
                             and after a restart the NIC attaches and serves HTTP
                             again.
    Expected result (fail):  the netif outlives end() (found=1), or the next
                             attach fails on its duplicate key.
    """
    device = peers["device"]
    _attach_and_use_network(dut, device)

    # end() with the netif attached and the device still on the bus.
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")

    # 'x' looks the netif up by its key; with nothing registered it stops nothing.
    dut.write("x")
    match = dut.expect(r"NETIF_STOP found=(\d)", timeout=5)
    found = match.group(1).decode() if isinstance(match.group(1), bytes) else match.group(1)
    assert found == "0", "the USB_NCM netif is still registered in esp_netif after end()"

    dut.write("G")
    _poll_state(dut, r"HOST_STATE running devices=[1-9]")
    _attach_and_use_network(dut, device)
