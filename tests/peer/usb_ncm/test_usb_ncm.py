def test_usb_ncm_enumeration(dut, peers):
    device = peers["device"]
    # DEVICE_BEGIN is a one-shot boot message; use the '?' handshake instead so
    # readiness does not depend on capture starting before boot.
    device.write("?")
    device.expect(r"DEVICE_READY ip=192\.168\.7\.1 link=1")

    dut.expect_exact("HOST_CONNECTED")
    dut.write("i")
    dut.expect("NCM_ENUM count=[1-9][0-9]* protocol=CDC-NCM complete=1")


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
