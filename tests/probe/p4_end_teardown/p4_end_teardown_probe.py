"""
Purpose:
    Find which part of a vendor bulk IN session makes end() fault on an ESP32-P4
    high-speed port.

    probe/p4_hs_fs_direct found that end() panics inside the host library's own
    interrupt path (proc_req_callback <- intr_hdlr_main) after such a session, in
    both the default and the forced-full-speed bus mode -- so the bus mode is not
    the variable. The sketch walks a ladder of increasingly complete sessions and
    prints STEP_END_ENTER before the call under test and STEP_END_OK after it, so
    the first step missing its OK line is the one that faults.

    The ladder index lives in RTC memory a panic reboot does not clear and is
    advanced before the step runs, so one flash walks the whole ladder even
    though a faulting step reboots the board.

Required hardware:
    - Two ESP32-P4 boards with their OTG HS ports wired together
    - This board (TEST_SERIAL_PORT_P4_END_TEARDOWN) is the host
    - The other runs tests/peer/usb_vendor_read/peer_device or the same protocol

Run from tests/:
    uv run --env-file .env pytest probe/p4_end_teardown/p4_end_teardown_probe.py -v -s

Reading the result:
    Step 3 (open_large_stream) faulting exonerates the read queue: the fault is in
    the continuous-read teardown that predates it. Step 4 implicates the queue's
    own teardown, step 5 only the case where the queue was left running.
"""

import re

BEGIN = re.compile(r"STEP_BEGIN index=(\d+) name=(\w+)")
ENTER = re.compile(r"STEP_END_ENTER name=(\w+)")
OK = re.compile(r"STEP_END_OK name=(\w+)")

STEPS = [
    "begin_end",
    "open_default",
    "open_large",
    "open_large_stream",
    "queue_stream_endqueue",
    "queue_stream_no_endqueue",
]


def test_p4_end_teardown_probe(dut):
    dut.expect(r"TEST_(?:BEGIN|RESUME) p4_end_teardown_probe", timeout=30)

    survived = {}
    for name in STEPS:
        dut.expect_exact(f"STEP_END_ENTER name={name}", timeout=60)
        try:
            dut.expect_exact(f"STEP_END_OK name={name}", timeout=25)
            survived[name] = True
            print(f"\n{name:26} end() returned")
        except Exception:
            survived[name] = False
            print(f"\n{name:26} end() DID NOT RETURN (fault)")
            # The board reboots and resumes at the next step, so keep reading.

    print("\n--- ladder ---")
    for name in STEPS:
        print(f"{name:26} {'ok' if survived.get(name) else 'FAULT'}")

    faulted = [name for name in STEPS if not survived.get(name)]
    assert not faulted, f"end() faulted after: {', '.join(faulted)}"
