"""
Purpose:
    Reproduce the ESP32-P4 role reversal fault in this repository, with no
    dependency on the EspUsbDevice repository's own test suite.

    A single ESP32-P4 runs both roles at once and then swaps them: the host
    starts on the full-speed controller with the device on the high-speed one,
    then device.end() and usb.end() run and the roles are exchanged. EspUsbHost
    2.9.0 aborts inside usb.end(); 2.8.0 completes the swap.

    The fault is in the IDF hub driver, reached from our own teardown: end() cuts
    root port power before the client closes the attached device, so the hub
    driver recycles a root port that is no longer powered and
    root_port_recycle() -- which handles only ENABLED and RECOVERY -- calls
    abort() on the default branch.

    Both conditions matter. The host must be on the FULL-speed port when end()
    runs, and the device must already be gone because device.end() ran first.
    probe/p4_end_teardown, which restarts a high-speed host with the peer still
    attached, passes on 2.9.0 and does not cover this.

Required hardware:
    - One ESP32-P4 with its two USB controllers wired for loopback
      (TEST_SERIAL_PORT_P4_LOOPBACK)
    - No second board: the same chip supplies both roles

Run from tests/:
    uv run --env-file .env pytest loopback/p4_role_reversal/p4_role_reversal_test.py -v -s

Reading the result:
    A fault shows up as REVERSE_HOST_END_ENTER with no REVERSE_HOST_END_OK after
    it -- the board panics inside usb.end() and reboots, so the next thing on the
    console is a fresh LOOPBACK_BEGIN. PHASE_REVERSE fail with both begin() calls
    reporting ok=1 is the other failure worth naming: the host restarted but the
    root port was left unpowered, so nothing enumerated.
"""


def test_p4_role_reversal(dut):
    """
    Expected result (pass):  The swap completes -- REVERSE_HOST_END_OK, both
                             begin() calls ok=1, PHASE_REVERSE ok, LOOPBACK_DONE.
    Expected result (fail):  usb.end() does not return (panic and reboot), or the
                             restarted host enumerates nothing.
    """
    dut.expect_exact("LOOPBACK_BEGIN p4_role_reversal", timeout=30)
    dut.expect_exact("NORMAL_KEYS", timeout=30)
    dut.expect_exact("NORMAL_LEDS", timeout=30)
    dut.expect_exact("PHASE_NORMAL ok", timeout=30)

    dut.expect_exact("REVERSE_HOST_END_ENTER", timeout=15)
    try:
        dut.expect_exact("REVERSE_HOST_END_OK", timeout=30)
    except Exception:
        raise AssertionError(
            "usb.end() did not return: the board panicked inside the teardown. "
            "This is the 2.9.0 role reversal regression."
        )

    def _ok(match):
        value = match.group(1)
        return value.decode() if isinstance(value, bytes) else value

    assert _ok(dut.expect(r"REVERSE_HOST_BEGIN ok=(\d)", timeout=30)) == "1", \
        "usb.begin() on the high-speed port failed after end()"
    assert _ok(dut.expect(r"REVERSE_DEVICE_BEGIN ok=(\d)", timeout=30)) == "1", \
        "device.begin() on the full-speed controller failed"

    dut.expect_exact("PHASE_REVERSE ok", timeout=30)
    dut.expect_exact("LOOPBACK_DONE p4_role_reversal", timeout=15)
