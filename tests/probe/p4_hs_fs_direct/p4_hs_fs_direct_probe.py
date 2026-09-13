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

    # Keyed on the mode rather than on order: a board that reboots mid-run prints
    # the ladder again from the top, and matching positionally then pairs a
    # forced-mode assertion with a default-mode row.
    rows = {}
    for mode in ("default", "fs_only"):
        match = dut.expect(
            rf"RESULT mode={mode} speed=(\w+) in_mps=(\d+) out_mps=(\d+) xfer=(\d+) "
            r"mbps=([\d.]+) bad=(\d+)",
            timeout=90,
        )
        speed, in_mps, out_mps, xfer, mbps, bad = (g.decode() for g in match.groups())
        rows[mode] = {
            "speed": speed,
            "in_mps": int(in_mps),
            "out_mps": int(out_mps),
            "xfer": int(xfer),
            "mbps": float(mbps),
            "bad": int(bad),
        }
        print(f"\n{mode:8} speed={speed:5} mps={in_mps}/{out_mps} xfer={xfer} "
              f"{float(mbps):7.3f} MB/s  bad={bad}")

    dut.expect_exact("PROBE_PASS forced=full default=high", timeout=60)

    default, forced = rows["default"], rows["fs_only"]

    assert default["speed"] == "high"
    assert forced["speed"] == "full", "the forced mode must not enumerate at high speed"

    # A bulk endpoint is 64 bytes at full speed and 512 at high speed. A device
    # reporting 512 on a full-speed bus would be describing a packet the bus
    # cannot carry, which is what makes this worth asserting rather than assuming.
    assert default["in_mps"] == 512, f"high speed should negotiate 512-byte bulk, got {default['in_mps']}"
    assert forced["in_mps"] == 64, f"full speed should negotiate 64-byte bulk, got {forced['in_mps']}"

    # The rate is the cross-check: a link reported as full speed that still moves
    # tens of MB/s would mean the report changed and the bus did not.
    assert forced["mbps"] < 2.0, (
        f"a full-speed bus cannot carry {forced['mbps']:.3f} MB/s -- the reported "
        "speed and the measured one disagree"
    )
    assert default["mbps"] > forced["mbps"] * 5, (
        f"high speed ({default['mbps']:.3f} MB/s) should be far above full speed "
        f"({forced['mbps']:.3f} MB/s) on the same link"
    )
    assert all(row["bad"] == 0 for row in rows.values()), "the ramp must arrive unbroken at either speed"
