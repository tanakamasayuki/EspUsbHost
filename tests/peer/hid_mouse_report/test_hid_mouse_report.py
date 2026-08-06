"""Report-protocol mouse: a peer that declares 16 buttons, 16-bit X/Y, a wheel
and AC Pan in an 8-byte report with no report ID, which is the layout issue #39
reports for a Logitech G502 HERO.

Decoded as a boot mouse report (what the library did before the report
descriptor was parsed) every one of these assertions fails: the button high byte
lands in X, the X low byte in Y and the X high byte in the wheel, and a report
that only moves Y produces no event at all because the three bytes the boot
layout reads are zero.
"""


def test_hid_mouse_report_axes(dut, peers):
    device = peers["device"]

    # 16-bit deltas, well past what an 8-bit boot axis can carry.
    device.write("r")
    dut.expect_exact("MOUSE x=300 y=0 wheel=0 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

    device.write("l")
    dut.expect_exact("MOUSE x=-300 y=0 wheel=0 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

    # The regression from the issue: a Y-only report must produce an event, and
    # must not leak into the wheel.
    device.write("d")
    dut.expect_exact("MOUSE x=0 y=300 wheel=0 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

    device.write("u")
    dut.expect_exact("MOUSE x=0 y=-300 wheel=0 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")


def test_hid_mouse_report_wheel_and_pan(dut, peers):
    device = peers["device"]

    device.write("w")
    dut.expect_exact("MOUSE x=0 y=0 wheel=1 pan=0 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")

    device.write("p")
    dut.expect_exact("MOUSE x=0 y=0 wheel=0 pan=-1 buttons=0 mask=0 previous=0 count=16 moved=1 changed=0")


def test_hid_mouse_report_buttons(dut, peers):
    device = peers["device"]

    device.write("m")
    dut.expect_exact("buttons=1 mask=1 previous=0 count=16 moved=0 changed=1")
    dut.expect_exact("buttons=0 mask=0 previous=1 count=16 moved=0 changed=1")

    device.write("R")
    dut.expect_exact("buttons=2 mask=2 previous=0 count=16 moved=0 changed=1")
    dut.expect_exact("buttons=0 mask=0 previous=2 count=16 moved=0 changed=1")

    # Button 16 lives in the high byte of the mask: invisible in `buttons`, which
    # keeps the low 8 for compatibility, and invisible entirely under the boot
    # layout, which reads that byte as X.
    device.write("x")
    dut.expect_exact("buttons=0 mask=32768 previous=0 count=16 moved=0 changed=1")
    dut.expect_exact("buttons=0 mask=0 previous=32768 count=16 moved=0 changed=1")
