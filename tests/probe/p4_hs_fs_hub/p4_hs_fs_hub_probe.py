"""
Purpose:
    Verify that HCFG.FSLSSUPP makes a high-speed-capable USB 2.0 hub enumerate
    at full speed on the ESP32-P4 HS physical port, and that an FS/LS device can
    then enumerate behind that hub without a Transaction Translator.

Required hardware:
    - ESP32-P4 host board (TEST_SERIAL_PORT_ESP32P4)
    - High-speed-capable USB 2.0 hub connected to the P4 HS port
    - One full-speed or low-speed device connected behind the hub

Run from tests/:
    uv run --env-file .env pytest probe/p4_hs_fs_hub/p4_hs_fs_hub_probe.py -v -s

Pass criteria:
    The hub is reported at full speed and a non-hub child is reported at full or
    low speed. A high-speed hub is an explicit failure because it means the
    speed-limiting bit did not take effect before reset negotiation.
"""


def test_p4_hs_fs_hub_probe(dut):
    dut.expect_exact("TEST_BEGIN p4_hs_fs_hub_probe")
    dut.expect_exact("HOST_READY port=hs bus_mode=full_speed_only", timeout=20)
    dut.expect_exact("PROBE_PASS hub_speed=full downstream_speed=fs_or_ls", timeout=45)
