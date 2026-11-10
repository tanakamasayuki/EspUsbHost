"""networkClose(), then the device leaves the bus, then the host restarts.

The order EspUsbDevice's tests found to break the next begin(). Its own module
because the peer ends its USB device here and is not brought back.
"""
import re
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


def _open_and_use_network(dut, device):
    _wait_device_link(device)
    dut.write("a")
    dut.expect_exact("NETWORK_ATTACH ok=1")
    dut.expect(r"ip=192\.168\.7\.\d", timeout=30)
    dut.write("g")
    dut.expect_exact("HTTP_GET code=200 body=ESPUSB_NCM_OK")


def _close_network(dut):
    dut.write("n")
    closed = dut.expect(r"NETWORK_CLOSE ready=(\d) devices=(\d+) error=(\S+)\r?\n", timeout=10)
    assert closed.group(1) == b"0", "networkReady() still true after networkClose()"
    assert closed.group(3) == b"ESP_OK", f"networkClose() left {closed.group(3).decode()}"


def _restart_host(dut):
    """end() then begin(); begin() must succeed and the host must run."""
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0", timeout=15)
    dut.write("G")
    state = _poll_state(dut, r"(HOST_BEGIN_FAILED \S+|HOST_STATE running devices=\d+)\r?\n", attempts=10)
    assert not state.group(1).startswith(b"HOST_BEGIN_FAILED"), state.group(1).decode()


def test_usb_ncm_close_device_end_then_host_restart(dut, peers):
    device = peers["device"]
    _open_and_use_network(dut, device)
    _close_network(dut)
    device.write("e")
    device.expect_exact("DEVICE_END")
    # The host must notice the device is gone before it is stopped.
    _poll_state(dut, r"HOST_STATE running devices=0", attempts=15)
    _restart_host(dut)
