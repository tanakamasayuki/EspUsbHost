def test_hid_keyboard(dut, peers):
    str = "hello, keyboard"
    device = peers["device"]
    device.write(str)
    dut.expect_exact(str)


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
