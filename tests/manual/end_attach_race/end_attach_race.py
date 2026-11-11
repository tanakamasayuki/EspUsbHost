"""Manual: end() while a device is attaching or detaching (ESP-IDF fault).

Not named test_*.py on purpose -- it fails on ESP-IDF v5.5.5, so it is run by
hand to check an ESP-IDF update, not as part of the suite. See manual/README.md.

    uv run --env-file .env pytest manual/end_attach_race/end_attach_race.py -v -s --combo s3
"""


def test_end_survives_bus_events(dut, peers):
    device = peers["device"]
    for _ in range(30):
        dut.write("?")
        try:
            dut.expect_exact("RACE_IDLE", timeout=1)
            break
        except Exception:
            continue
    device.write("T")
    device.expect_exact("DEVICE_TOGGLING")
    dut.write("S")
    done = dut.expect(r"RACE_DONE cycles=\d+ connects=(\d+) begin_failures=(\d+)", timeout=600)
    device.write("U")
    print(done.group(0))
    assert int(done.group(2)) == 0
