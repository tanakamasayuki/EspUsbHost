import re
import time

import pytest


def _poll_state(dut, pattern, attempts=100):
    """Wait for a state the sketch answers on demand.

    Polling a query rather than waiting for a connect line matters because a
    connect line is printed once, when the device enumerates, so waiting for it
    only works while the test doing so happens to run first.
    """
    for _ in range(attempts):
        dut.write("Q")
        try:
            dut.expect(pattern, timeout=2)
            return
        except Exception:
            continue
    raise AssertionError(f"the host never reported {pattern!r}")


@pytest.fixture(autouse=True)
def usb_host(dut, peers):
    """Start the USB host for this test, and stop it however the test ends.

    The sketch does not start it in setup(): the peer board is flashed after this
    board has booted, and a host that is already running observes those resets
    and records the enumerations they cause as errors.

    `peers` is requested for its ordering, not its value. This fixture is autouse
    and would otherwise be set up before it, which starts the host before the peer
    upload -- putting the host back inside exactly the window the gating exists to
    avoid. Requesting `peers` moves the upload ahead of the start.

    Stopping in the teardown rather than at the end of the test body means it
    runs when the test fails or is interrupted too, so the board is not left
    hosting USB after the run.
    """
    _poll_state(dut, r"HOST_STATE (?:idle|running) devices=\d+")
    dut.write("G")
    # Wait for enumeration. This module's tests do not read the connect-time
    # output, so polling here consumes nothing they need.
    _poll_state(dut, r"HOST_STATE running devices=[1-9]")
    yield
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")




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


def _wait_ncm_enumeration(dut, attempts=50):
    """Return the host's enumeration report once a device has been enumerated."""
    for _ in range(attempts):
        dut.write("i")
        # Match through the end of the line. expect() returns as soon as the
        # buffer allows a match, so a pattern ending in a variable-length class is
        # satisfied by a prefix and captures however much of the line has arrived
        # -- which cut "claim_attempts=0 claimed=0 managed=0 error=ESP_OK" down to
        # "claim_attempts=0 claimed=0 managed". Requiring the newline makes the
        # match wait for the whole line. [^\r\n] rather than . so it cannot run
        # past the line either.
        report = dut.expect(r"NCM_ENUM [^\r\n]*\r?\n", timeout=5).group(0).decode().strip()
        if not report.startswith("NCM_ENUM count=0"):
            return report
        time.sleep(0.2)
    raise AssertionError("the host never reported an enumerated NCM device")


def test_usb_ncm_dhcp_and_http_then_discovery_does_not_claim(dut, peers):
    """The link works, and a host that has not been asked to attach claims nothing.

    The order here is deliberate, and it is the opposite of how this test used to
    read. Attaching first is what makes the second half mean something: it drives
    the claim counters off zero and the test sees them move, so when the host is
    restarted and they read zero again, that zero is one the host produced rather
    than one the sketch would print no matter what.

    Asserting the pristine state *first* -- which is what this test did -- is weaker
    in two ways. It only held while nothing had attached before it, so adding a
    test above it made it fail; and even after that was fixed by rebuilding the
    state, nothing in the test showed the counters could ever be non-zero, so a
    sketch that always reported zero would have passed just the same.
    """
    device = peers["device"]

    _wait_device_link(device)

    # Poll rather than waiting for HOST_CONNECTED: that line is printed once, when
    # the device enumerates, so waiting for it would only work while this is the
    # first test in the module. The query answers "count=0" until enumeration
    # finishes.
    _wait_ncm_enumeration(dut)

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

    # Attaching claimed the interface, so the counters must now be off zero. This
    # is the half that gives the assertion below its teeth: it shows the fields
    # move, which a report that is hard-wired to zero could not do.
    attached = _wait_ncm_enumeration(dut)
    assert re.search(r"claim_attempts=[1-9]", attached), attached
    assert "claim_attempts=0 claimed=0 managed=0" not in attached, attached

    # Put the host back to where nothing has been attached. The flags live on the
    # device object begin() creates, so end() discards them with the device, and
    # onDeviceDisconnected() clears the attach as well. Costs one re-enumeration.
    dut.write("H")
    dut.expect_exact("HOST_STATE idle devices=0")
    dut.write("G")
    _poll_state(dut, r"HOST_STATE running devices=[1-9]")

    _wait_device_link(device)

    # Discovery must report the candidate and claim nothing.
    report = _wait_ncm_enumeration(dut)
    assert re.fullmatch(
        r"NCM_ENUM count=[1-9][0-9]* protocol=CDC-NCM complete=1 "
        r"ctrl=\d+ data=\d+ alt=\d+ in=0x[0-9a-f]+ out=0x[0-9a-f]+ "
        r"notify=0x[0-9a-f]+ claim_attempts=0 claimed=0 managed=0 error=ESP_OK",
        report,
    ), report
