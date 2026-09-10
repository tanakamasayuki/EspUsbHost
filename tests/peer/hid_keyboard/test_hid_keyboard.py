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














# arduino-esp32 の USBHID.cpp (tinyusb_get_device_by_report_id) は reports_num==0 の
# デバイスを検索対象から除外する。USBHIDKeyboard が使う TUD_HID_REPORT_DESC_KEYBOARD を
# esp_hid_parse_report_map がすべて BOOT mode と判定するため reports_num が 0 になり、
# LED SET_REPORT 受信時のコールバック (_onSetFeature/_onOutput) が呼ばれない。
# Peer 側での回避は不可能で、USBHID.cpp へのパッチが必要なため自動テストから除外する。
def skip_test_hid_keyboard_led(dut, peers):
    device = peers["device"]

    dut.write("n")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=1 capslock=0 scrolllock=0")

    dut.write("c")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=1 scrolllock=0")

    dut.write("s")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=0 scrolllock=1")

    dut.write("0")
    dut.expect_exact("LED_TX 1")
    device.expect_exact("LED numlock=0 capslock=0 scrolllock=0")


def test_hid_keyboard(dut, peers, run_checks):
    def basic():
        str = "hello, keyboard"
        device = peers["device"]
        dut.write("q")
        dut.expect_exact("VERBOSE 0")
        device.write(str)
        dut.expect_exact(str)

    def shift_boot_reports():
        device = peers["device"]

        dut.write("e")
        dut.expect_exact("LAYOUT EN_US")
        device.write("@")
        device.expect_exact("SEND @ 1")
        dut.expect_exact("HID_INPUT modifier=0x02 reserved=0x00 key0=0x1f len=8")
        dut.expect_exact("KEY_STATE modifiers=0x02 a_down=0 a_pressed=0 a_released=0 lctrl_down=0 lctrl_pressed=0 lctrl_released=0")
        dut.expect_exact("RAW_KEY keycode=0x1f ascii=0x40 modifiers=0x02")
        dut.expect_exact("KEY @")

        dut.write("j")
        dut.expect_exact("LAYOUT JA_JP")
        device.write("_")
        device.expect_exact("SEND _ 1")
        dut.expect_exact("HID_INPUT modifier=0x02 reserved=0x00 key0=0x87 len=8")
        dut.expect_exact("RAW_KEY keycode=0x87 ascii=0x5f modifiers=0x02")
        dut.expect_exact("KEY _")

    def altgr():
        device = peers["device"]

        dut.write("d")
        dut.expect_exact("LAYOUT DE_DE")

        # AltGr (Right Alt, 0x40) + Q produces '@' on the German layout: an
        # ASCII-representable character, so both ascii and unicode are 0x40.
        device.write("{")
        device.expect_exact("SEND ALTGR_Q 1")
        dut.expect_exact("HID_INPUT modifier=0x40 reserved=0x00 key0=0x14 len=8")
        dut.expect_exact("RAW_KEY keycode=0x14 ascii=0x40 modifiers=0x40 unicode=0x0040")
        dut.expect_exact("KEY @")

        # AltGr + E produces € (U+20AC): outside Latin-1, so unicode carries it and
        # the ascii byte is 0.
        device.write("}")
        device.expect_exact("SEND ALTGR_E 1")
        dut.expect_exact("HID_INPUT modifier=0x40 reserved=0x00 key0=0x08 len=8")
        dut.expect_exact("RAW_KEY keycode=0x08 ascii=0x00 modifiers=0x40 unicode=0x20ac")

    def capslock():
        device = peers["device"]

        dut.write("d")
        dut.expect_exact("LAYOUT DE_DE")

        # CapsLock ON, then the ü key -> Ü. This is an accented letter outside the
        # US a-z usage range, which the old positional CapsLock could not reach.
        device.write("<")
        device.expect_exact("SEND CAPS 1")
        device.write(">")
        device.expect_exact("SEND KEY_UE 1")
        dut.expect_exact("RAW_KEY keycode=0x2f ascii=0xdc modifiers=0x00 unicode=0x00dc")

        # CapsLock OFF again, same key -> ü.
        device.write("<")
        device.expect_exact("SEND CAPS 1")
        device.write(">")
        device.expect_exact("SEND KEY_UE 1")
        dut.expect_exact("RAW_KEY keycode=0x2f ascii=0xfc modifiers=0x00 unicode=0x00fc")

    def state_modifier_only():
        device = peers["device"]

        dut.write("e")
        dut.expect_exact("LAYOUT EN_US")
        device.write("^")
        device.expect_exact("SEND LCTRL 1")
        dut.expect_exact("KEY_STATE modifiers=0x01 a_down=0 a_pressed=0 a_released=0 lctrl_down=1 lctrl_pressed=1 lctrl_released=0")
        dut.expect_exact("KEY_STATE modifiers=0x00 a_down=0 a_pressed=0 a_released=0 lctrl_down=0 lctrl_pressed=0 lctrl_released=1")

    def listeners():
        device = peers["device"]

        dut.write("l")
        dut.expect_exact("LISTENER_SETUP empty=1 ids=1 capacity=1 invalid_remove=1 state=1 max=4")

        # The callback and listeners use the same event snapshot. Listener 1 removes
        # itself and adds listener 5, but listener 5 must not run for this event.
        device.write("a")
        dut.expect_exact("STATE_LISTENER a_down=1 changed=1")
        dut.expect_exact("LISTENER PRIMARY")
        dut.expect_exact("LISTENER 1")
        dut.expect_exact("LISTENER 2")
        dut.expect_exact("LISTENER_STATE count=1")
        dut.expect_exact("STATE_LISTENER a_down=0 changed=1")

        # The mutation takes effect on the next event, preserving registration
        # order: the existing listener 2 runs before the newly-added listener 5.
        device.write("b")
        dut.expect_exact("STATE_LISTENER a_down=0 changed=1")
        dut.expect_exact("LISTENER PRIMARY")
        dut.expect_exact("LISTENER 2")
        dut.expect_exact("LISTENER_STATE count=2")
        dut.expect_exact("LISTENER 5")

        dut.write("u")
        dut.expect_exact("LISTENER_REMOVE removed=1 second=1")
        device.write("c")
        dut.expect_exact("LISTENER PRIMARY")
        dut.expect_exact("LISTENER_STATE count=3")
        dut.expect_exact("LISTENER 5")

        dut.write("x")
        dut.expect_exact("LISTENER_CLEAR 1")

    run_checks([
        basic,
        shift_boot_reports,
        altgr,
        capslock,
        state_modifier_only,
        listeners,
    ])
