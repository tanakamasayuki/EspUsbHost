import time


def _wait_device_link(device, timeout=15):
    """Wait for the peer to observe the host's SET_CONFIGURATION request.

    The peer is uploaded and started after the host, so its serial console can
    become ready before USB enumeration reaches the configured state.
    """
    deadline = time.monotonic() + timeout
    while True:
        device.write("?")
        match = device.expect(
            r"DEVICE_READY ip=192\.168\.7\.1 link=(\d)",
            timeout=min(2, max(0.1, deadline - time.monotonic())),
        )
        if int(match.group(1)) == 1:
            return
        if time.monotonic() >= deadline:
            raise AssertionError(f"USB NCM device link did not come up within {timeout}s")
        time.sleep(0.1)


def test_usb_ncm_enumeration(dut, peers):
    device = peers["device"]
    _wait_device_link(device)

    dut.expect_exact("HOST_CONNECTED")
    dut.write("i")
    dut.expect(
        r"NCM_ENUM count=[1-9][0-9]* protocol=CDC-NCM complete=1 "
        r"ctrl=\d+ data=\d+ alt=\d+ in=0x[0-9a-f]+ out=0x[0-9a-f]+ "
        r"notify=0x[0-9a-f]+ claim_attempts=0 claimed=0 managed=0 error=ESP_OK"
    )


def test_usb_ncm_dhcp_and_http(dut, peers):
    device = peers["device"]
    device.write("?")
    device.expect_exact("DEVICE_READY ip=192.168.7.1")

    # Attach the USB NIC as a DHCP-client netif and wait for the lease. The host
    # auto-reports it as "NETWORK_IP ip=192.168.7.x" once, so match the address
    # directly (no intervening expects that would consume that one-shot line).
    dut.write("a")
    dut.expect_exact("NETWORK_ATTACH ok=1")
    dut.expect(r"ip=192\.168\.7\.\d", timeout=30)

    # Fetch the fixed page the device serves over the USB CDC-NCM link.
    dut.write("g")
    dut.expect_exact("HTTP_GET code=200 body=ESPUSB_NCM_OK")
