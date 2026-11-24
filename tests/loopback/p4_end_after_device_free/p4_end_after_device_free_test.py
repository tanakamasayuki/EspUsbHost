"""
Purpose:
    usb.end() called the instant the host library has freed a device that went
    away, before the hub driver has run the root port recovery that the free
    queued. Powering the port off in that window made the queued
    hcd_port_recover() fail with ESP_ERR_INVALID_STATE, and hub.c root_port_req
    aborted on its ESP_ERROR_CHECK (espressif/usb 1.6.1). EspUsbHost 2.11.0 hit
    it once in EspUsbDevice's ESP32-P4 loopback CCID test.

    The window is a few microseconds long, so the sketch aims at it instead of
    waiting for luck: the host tasks run on core 0 and the sketch spins on core 1
    reading usb_host_lib_info(), calling usb.end() as soon as the device count
    reaches 0. Ten cycles alternate the host between the full-speed and the
    high-speed port, and each cycle enumerates again, so a teardown that left the
    port unusable fails as well as one that aborts.

Required hardware:
    - One ESP32-P4 with its two USB controllers wired for loopback
      (TEST_SERIAL_PORT_P4_LOOPBACK)

Run from tests/:
    uv run --env-file .env pytest --combo p4 loopback/p4_end_after_device_free -v
"""


def _text(value):
    return value.decode() if isinstance(value, bytes) else value


def test_p4_end_after_device_free(dut):
    """
    Expected result (pass):  every cycle enumerates and its end() returns.
    Expected result (fail):  the board aborts inside the teardown (no CYCLE_END),
                             or the next cycle enumerates nothing.
    """
    for _ in range(30):
        dut.write("S")
        try:
            dut.expect_exact("LOOPBACK_BEGIN p4_end_after_device_free", timeout=1)
            break
        except Exception:
            continue
    else:
        raise AssertionError("the sketch never started")

    for cycle in range(1, 11):
        match = dut.expect(rf"CYCLE_ENUM {cycle} host=(fs|hs) ok=(\d)", timeout=30)
        host, ok = (_text(v) for v in match.groups())
        assert ok == "1", f"cycle {cycle} (host on {host}) enumerated nothing"
        try:
            match = dut.expect(rf"CYCLE_END {cycle} freed=(\d)", timeout=30)
        except Exception:
            raise AssertionError(
                f"cycle {cycle} (host on {host}): usb.end() did not return, the board "
                "aborted in the teardown"
            )
        assert _text(match.group(1)) == "1", f"cycle {cycle}: the host library never freed the device"
    dut.expect_exact("LOOPBACK_DONE p4_end_after_device_free", timeout=15)
