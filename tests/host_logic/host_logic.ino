#include "EspUsbHostHid.h"

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition) {
    Serial.print("PASS ");
    Serial.println(name);
    passCount++;
  } else {
    Serial.print("FAIL ");
    Serial.println(name);
    failCount++;
  }
}

static EspUsbHostKeyboardReport report(uint8_t modifier,
                                       uint8_t key0,
                                       uint8_t key1 = 0,
                                       uint8_t key2 = 0,
                                       uint8_t key3 = 0,
                                       uint8_t key4 = 0,
                                       uint8_t key5 = 0)
{
  EspUsbHostKeyboardReport value;
  value.data[0] = modifier;
  value.data[2] = key0;
  value.data[3] = key1;
  value.data[4] = key2;
  value.data[5] = key3;
  value.data[6] = key4;
  value.data[7] = key5;
  return value;
}

static void testKeycodeToAscii()
{
  check(espUsbHostKeycodeToAscii(0x04, 0x00, ESP_USB_HOST_KEYBOARD_LAYOUT_US) == 'a', "key_us_a");
  check(espUsbHostKeycodeToAscii(0x04, 0x02, ESP_USB_HOST_KEYBOARD_LAYOUT_US) == 'A', "key_us_shift_a");
  check(espUsbHostKeycodeToAscii(0x1f, 0x02, ESP_USB_HOST_KEYBOARD_LAYOUT_US) == '@', "key_us_shift_2");
  check(espUsbHostKeycodeToAscii(0x1f, 0x02, ESP_USB_HOST_KEYBOARD_LAYOUT_JP) == '"', "key_jp_shift_2");
  check(espUsbHostKeycodeToAscii(0x59, 0x02, ESP_USB_HOST_KEYBOARD_LAYOUT_JP) == '1', "keypad_1_shift_ignored");
  check(espUsbHostKeycodeToAscii(0x63, 0x00, ESP_USB_HOST_KEYBOARD_LAYOUT_US) == '.', "keypad_dot");
}

static void testKeyboardReportValidation()
{
  const uint8_t valid[8] = {0x00, 0x00, 0x04, 0, 0, 0, 0, 0};
  const uint8_t invalidReserved[8] = {0x00, 0x01, 0x04, 0, 0, 0, 0, 0};
  const uint8_t invalidKeycode[8] = {0x00, 0x00, 0xe0, 0, 0, 0, 0, 0};
  check(espUsbHostIsBootKeyboardReportValid(valid, sizeof(valid)), "keyboard_report_valid");
  check(!espUsbHostIsBootKeyboardReportValid(invalidReserved, sizeof(invalidReserved)), "keyboard_report_reserved_invalid");
  check(!espUsbHostIsBootKeyboardReportValid(invalidKeycode, sizeof(invalidKeycode)), "keyboard_report_keycode_invalid");
  check(!espUsbHostIsBootKeyboardReportValid(valid, 7), "keyboard_report_short_invalid");
}

static void testKeyboardDiff()
{
  EspUsbHostKeyboardEvent events[ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS];

  EspUsbHostKeyboardReport empty = report(0, 0);
  EspUsbHostKeyboardReport pressA = report(0, 0x04);
  size_t count = espUsbHostBuildKeyboardEvents(1, empty, pressA, ESP_USB_HOST_KEYBOARD_LAYOUT_US, events, ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS);
  check(count == 1 && events[0].pressed && !events[0].released && events[0].keycode == 0x04 && events[0].ascii == 'a', "keyboard_press_a");

  EspUsbHostKeyboardReport pressAB = report(0, 0x04, 0x05);
  count = espUsbHostBuildKeyboardEvents(1, pressA, pressAB, ESP_USB_HOST_KEYBOARD_LAYOUT_US, events, ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS);
  check(count == 1 && events[0].pressed && events[0].keycode == 0x05 && events[0].ascii == 'b', "keyboard_press_b_while_a_held");

  count = espUsbHostBuildKeyboardEvents(1, pressAB, pressA, ESP_USB_HOST_KEYBOARD_LAYOUT_US, events, ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS);
  check(count == 1 && events[0].released && events[0].keycode == 0x05, "keyboard_release_b");

  EspUsbHostKeyboardReport shiftA = report(0x02, 0x04);
  count = espUsbHostBuildKeyboardEvents(1, empty, shiftA, ESP_USB_HOST_KEYBOARD_LAYOUT_US, events, ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS);
  check(count == 1 && events[0].pressed && events[0].ascii == 'A' && events[0].modifiers == 0x02, "keyboard_shift_a");
}

static void testMouseReport()
{
  EspUsbHostMouseEvent event;

  const uint8_t idle[3] = {0x00, 0x00, 0x00};
  check(!espUsbHostParseBootMouseReport(2, idle, sizeof(idle), 0x00, event), "mouse_idle_no_event");

  const uint8_t move[4] = {0x00, 0x05, 0xfd, 0x01};
  check(espUsbHostParseBootMouseReport(2, move, sizeof(move), 0x00, event) &&
        event.interfaceNumber == 2 &&
        event.moved &&
        !event.buttonsChanged &&
        event.x == 5 &&
        event.y == -3 &&
        event.wheel == 1,
        "mouse_move_wheel");

  const uint8_t press[3] = {ESP_USB_HOST_MOUSE_LEFT, 0x00, 0x00};
  check(espUsbHostParseBootMouseReport(2, press, sizeof(press), 0x00, event) &&
        !event.moved &&
        event.buttonsChanged &&
        event.buttons == ESP_USB_HOST_MOUSE_LEFT &&
        event.previousButtons == 0x00,
        "mouse_button_press");

  const uint8_t release[3] = {0x00, 0x00, 0x00};
  check(espUsbHostParseBootMouseReport(2, release, sizeof(release), ESP_USB_HOST_MOUSE_LEFT, event) &&
        event.buttonsChanged &&
        event.buttons == 0x00 &&
        event.previousButtons == ESP_USB_HOST_MOUSE_LEFT,
        "mouse_button_release");

  check(!espUsbHostParseBootMouseReport(2, press, 2, 0x00, event), "mouse_short_invalid");
}

static void testKeyboardLedReport()
{
  check(espUsbHostBuildKeyboardLedReport(false, false, false) == 0x00, "keyboard_led_none");
  check(espUsbHostBuildKeyboardLedReport(true, false, false) == ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK, "keyboard_led_num");
  check(espUsbHostBuildKeyboardLedReport(false, true, false) == ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK, "keyboard_led_caps");
  check(espUsbHostBuildKeyboardLedReport(false, false, true) == ESP_USB_HOST_KEYBOARD_LED_SCROLL_LOCK, "keyboard_led_scroll");
  check(espUsbHostBuildKeyboardLedReport(true, true, true) ==
          (ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK |
           ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK |
           ESP_USB_HOST_KEYBOARD_LED_SCROLL_LOCK),
        "keyboard_led_all");
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN host_logic");
  testKeycodeToAscii();
  testKeyboardReportValidation();
  testKeyboardDiff();
  testMouseReport();
  testKeyboardLedReport();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void loop()
{
}
