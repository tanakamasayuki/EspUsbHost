"""
Purpose:
    Test the foundation of docs/p4-hs-port-fs-only-hub.ja.md without a hub: does
    HCFG.FSLSSUPP actually bring the ESP32-P4 high-speed physical port up as a
    full-speed bus, and does a device wired straight to it still enumerate there?

    That document proposed the mechanism so a high-speed-capable USB 2.0 hub
    would enumerate as a full-speed hub, letting FS/LS devices behind it work
    without a Transaction Translator -- which the P4 HS DWC has no hardware for.
    It was written with no hardware result. A hub adds its own variables (does
    the hub accept a full-speed upstream at all?), so this probe removes them:
    one device, one cable, the same link measured twice.

    The sketch runs begin()/end() twice, forced full speed and then the default,
    and reports the enumerated speed and the bulk IN rate of each. The rate is
    the cross-check: a link reported as full speed that moves 24 MB/s would mean
    the report, not the bus, is what changed.

Required hardware:
    - Two ESP32-P4 boards with their OTG HS ports wired together (A-A)
    - This board (TEST_SERIAL_PORT_P4_FS_DIRECT) is the host
    - The other runs tests/peer/usb_vendor_read/peer_device, built for its
      p4_peer_device profile, flashed separately before this run

Run from tests/:
    uv run --env-file .env pytest probe/p4_hs_fs_direct/p4_hs_fs_direct_probe.py -v -s

Pass criteria:
    Forced mode enumerates the peer at full speed and the default mode at high
    speed. A high-speed enumeration in forced mode is an explicit failure: it
    means the bit did not take effect before reset negotiation, which is the one
    thing the proposal depends on.
"""

import re

RESULT = re.compile(
    r"RESULT mode=(\w+) speed=(\w+) in_mps=(\d+) out_mps=(\d+) xfer=(\d+) mbps=([\d.]+) bad=(\d+)"
)


def test_p4_hs_fs_direct_probe(dut):
    dut.expect_exact("TEST_BEGIN p4_hs_fs_direct_probe")

    ENUM = re.compile(r"ENUM mode=(\w+) speed=(\w+) in_mps=(\d+) out_mps=(\d+) xfer=(\d+)")

    dut.expect_exact("HOST_READY port=hs bus_mode=default", timeout=30)
    default_enum = dut.expect(ENUM, timeout=60)
    default = dut.expect(RESULT, timeout=60)

    dut.expect_exact("HOST_READY port=hs bus_mode=full_speed_only", timeout=30)
    forced_enum = dut.expect(ENUM, timeout=60)
    forced = dut.expect(RESULT, timeout=60)

    for match in (default_enum, forced_enum):
        mode, speed, in_mps, out_mps, xfer = (g.decode() for g in match.groups())
        print(f"\nENUM {mode:8} speed={speed:5} in_mps={in_mps} out_mps={out_mps} xfer={xfer}")

    rows = []
    for match in (default, forced):
        mode, speed, in_mps, out_mps, xfer, mbps, bad = (g.decode() for g in match.groups())
        rows.append((mode, speed, int(in_mps), float(mbps), int(bad)))
        print(f"\n{mode:8} speed={speed:5} mps={in_mps}/{out_mps} xfer={xfer} "
              f"{float(mbps):7.3f} MB/s  bad={bad}")

    dut.expect_exact("PROBE_PASS forced=full default=high", timeout=30)

    # The speeds are the verdict; the rates say whether the bus followed them.
    default_rate = rows[0][3]
    forced_rate = rows[1][3]
    assert forced_rate < 2.0, (
        f"a full-speed bus cannot carry {forced_rate:.3f} MB/s -- the reported "
        "speed and the measured one disagree"
    )
    assert default_rate > forced_rate * 5, (
        f"high speed ({default_rate:.3f} MB/s) should be far above full speed "
        f"({forced_rate:.3f} MB/s) on the same link"
    )
    assert all(bad == 0 for *_, bad in rows), "the ramp must arrive unbroken at either speed"

    # A bulk endpoint is 64 bytes at full speed and 512 at high speed. A device
    # that reported 512 on a full-speed bus would be describing a packet the bus
    # cannot carry, which is what makes this worth asserting rather than assuming.
    assert rows[0][2] == 512, f"high speed should negotiate 512-byte bulk, got {rows[0][2]}"
    assert rows[1][2] == 64, f"full speed should negotiate 64-byte bulk, got {rows[1][2]}"
