"""
Purpose:
    Work out what the CH335F hub (1a86:8094) plus an ALIENTEK DP100 needs to crash
    the ESP-IDF hub driver, and specifically whether this library holding the hub
    open as a client is a necessary ingredient. Each part alone is fine: the hub
    with other devices, the DP100 on another hub or connected directly.

    The sketch runs 12 s with hub tracking off (the library never opens the external
    hub), then turns it on - the shipped default, and the configuration that
    crashed. Built with DebugLevel=verbose so the hub driver's own messages are in
    the log.

    Reading it:
      - phase 1 clean, phase 2 crashes -> the client handle is a necessary
        ingredient, and `setHubTrackingEnabled(false)` is both the diagnosis and a
        workaround. The fix to weigh is whether the library should hold that handle
        at all.
      - both phases crash -> the library is not involved; it is the hub driver and
        this hub, and the answer is to avoid the combination and report it upstream.
      - neither crashes -> the race did not land this run. It is intermittent: the
        same sketch survived twice before the run that caught it.

Why a probe:
    Exploratory, and the answer is in the log rather than in a pass/fail.

Safety:
    The sketch always boots with tracking off, so a board that crashes in phase 2
    still comes up in the harmless phase and stays flashable.

Required hardware:
    - ESP32-S3 host board (TEST_SERIAL_PORT_ESP32S3)
    - The CH335F hub with the DP100 behind it

Run:
    uv run --env-file .env pytest probe/hub_enum/hub_enum_probe.py -v -s
"""


def test_hub_enum_probe(dut):
    """
    Expected result:  Both phases logged with their device reports. Which phase the
                      assert lands in - if it lands at all - is the finding.
    """
    dut.expect_exact("TEST_BEGIN hub_enum_probe")
    dut.expect_exact("PHASE 1 END", timeout=40)
    dut.expect_exact("PROBE_DONE", timeout=60)
