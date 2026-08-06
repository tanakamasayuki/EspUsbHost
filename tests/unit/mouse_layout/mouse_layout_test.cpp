// Host tests for the mouse report descriptor parser and report decoder.
// src/EspUsbHostHidLayout.h is deliberately free of Arduino and USB
// dependencies, so the production header is included directly and the shipped
// code itself is exercised.

#include "EspUsbHostHidLayout.h"

#include <cstdio>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
  if (!condition)
  {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

void checkEqual(long actual, long expected, const char *what)
{
  if (actual != expected)
  {
    printf("FAIL: %s (actual=%ld, expected=%ld)\n", what, actual, expected);
    failures++;
  }
}

// The 4-byte boot mouse layout as a report descriptor: 3 buttons, 5 bits of
// padding, three 8-bit relative axes. What almost every plain mouse declares,
// and the layout the library assumed for every mouse before this parser.
const uint8_t kBootMouseDescriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xa1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xa1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Button)
    0x19, 0x01,       //     Usage Minimum (1)
    0x29, 0x03,       //     Usage Maximum (3)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x03,       //     Report Count (3)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x05,       //     Report Size (5)
    0x81, 0x03,       //     Input (Const,Var,Abs) - padding
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x09, 0x38,       //     Usage (Wheel)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7f,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x03,       //     Report Count (3)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0xc0,             //   End Collection
    0xc0,             // End Collection
};

// The layout issue #39 reports for a Logitech G502 HERO: 16 buttons, 16-bit
// X/Y, 8-bit wheel and AC Pan, in an 8-byte report with no report ID. Decoding
// this as a boot mouse puts the button high byte in X, the X low byte in Y and
// the X high byte in the wheel, which is exactly what was observed.
const uint8_t kGamingMouseDescriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xa1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xa1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Button)
    0x19, 0x01,       //     Usage Minimum (1)
    0x29, 0x10,       //     Usage Maximum (16)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x10,       //     Report Count (16)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x16, 0x01, 0x80, //     Logical Minimum (-32767)
    0x26, 0xff, 0x7f, //     Logical Maximum (32767)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x02,       //     Report Count (2)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7f,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x01,       //     Report Count (1)
    0x09, 0x38,       //     Usage (Wheel)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0x05, 0x0c,       //     Usage Page (Consumer)
    0x0a, 0x38, 0x02, //     Usage (AC Pan)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0xc0,             //   End Collection
    0xc0,             // End Collection
};

void testBootMouseDescriptor()
{
  EspUsbHostMouseReportLayout layout;
  check(espUsbHostParseMouseReportLayout(kBootMouseDescriptor, sizeof(kBootMouseDescriptor), layout),
        "boot mouse descriptor parses");
  checkEqual(layout.reportId, 0, "boot mouse has no report ID");
  checkEqual(layout.buttonCount, 3, "boot mouse button count");
  checkEqual(layout.buttonsBitOffset, 0, "boot mouse buttons at bit 0");
  checkEqual(layout.x.bitOffset, 8, "boot mouse X at bit 8");
  checkEqual(layout.x.bitSize, 8, "boot mouse X is 8-bit");
  check(layout.x.isSigned, "boot mouse X is signed");
  checkEqual(layout.y.bitOffset, 16, "boot mouse Y at bit 16");
  checkEqual(layout.wheel.bitOffset, 24, "boot mouse wheel at bit 24");
  check(!layout.pan.present, "boot mouse has no AC Pan");
  checkEqual(layout.reportBits, 32, "boot mouse report is 4 bytes");

  // Decoding the boot report through the learned layout must agree with the
  // fixed boot parsing it replaces.
  const uint8_t report[] = {0x05, 0xfb, 0x02, 0xff};
  EspUsbHostMouseReportValues values;
  check(espUsbHostDecodeMouseReport(layout, report, sizeof(report), values), "boot report decodes");
  checkEqual(values.buttons, 0x05, "boot buttons 1 and 3");
  checkEqual(values.x, -5, "boot X");
  checkEqual(values.y, 2, "boot Y");
  checkEqual(values.wheel, -1, "boot wheel");
  checkEqual(values.pan, 0, "boot pan stays 0");
}

void testGamingMouseDescriptor()
{
  EspUsbHostMouseReportLayout layout;
  check(espUsbHostParseMouseReportLayout(kGamingMouseDescriptor, sizeof(kGamingMouseDescriptor), layout),
        "gaming mouse descriptor parses");
  checkEqual(layout.reportId, 0, "gaming mouse has no report ID");
  checkEqual(layout.buttonCount, 16, "16 buttons");
  checkEqual(layout.buttonsBitOffset, 0, "buttons at bit 0");
  checkEqual(layout.x.bitOffset, 16, "X after the button word");
  checkEqual(layout.x.bitSize, 16, "X is 16-bit");
  checkEqual(layout.y.bitOffset, 32, "Y at bit 32");
  checkEqual(layout.y.bitSize, 16, "Y is 16-bit");
  checkEqual(layout.wheel.bitOffset, 48, "wheel at bit 48");
  checkEqual(layout.wheel.bitSize, 8, "wheel is 8-bit");
  check(layout.pan.present, "AC Pan is present");
  checkEqual(layout.pan.bitOffset, 56, "AC Pan at bit 56");
  checkEqual(layout.reportBits, 64, "report is 8 bytes");

  // buttons | X = +10 | Y = -10 | wheel | pan
  const uint8_t report[] = {0x00, 0x02, 0x0a, 0x00, 0xf6, 0xff, 0x01, 0xff};
  EspUsbHostMouseReportValues values;
  check(espUsbHostDecodeMouseReport(layout, report, sizeof(report), values), "gaming report decodes");
  checkEqual(values.buttons, 0x0200, "button 10 held");
  checkEqual(values.x, 10, "X = +10");
  checkEqual(values.y, -10, "Y = -10");
  checkEqual(values.wheel, 1, "wheel = +1");
  checkEqual(values.pan, -1, "pan = -1");

  // The regression from issue #39: a report that only moves Y decoded as a boot
  // mouse looks completely idle (bytes 1..3 are all zero), so no event was
  // emitted at all. Through the layout it is a Y movement.
  const uint8_t yOnly[] = {0x00, 0x00, 0x00, 0x00, 0xfb, 0xff, 0x00, 0x00};
  check(espUsbHostDecodeMouseReport(layout, yOnly, sizeof(yOnly), values), "Y-only report decodes");
  checkEqual(values.x, 0, "Y-only: X stays 0");
  checkEqual(values.y, -5, "Y-only: Y = -5");
  checkEqual(values.wheel, 0, "Y-only: wheel stays 0");
  checkEqual(values.buttons, 0, "Y-only: no buttons");

  // ... and X movement must not leak into the wheel.
  const uint8_t xOnly[] = {0x00, 0x00, 0x2c, 0x01, 0x00, 0x00, 0x00, 0x00};
  check(espUsbHostDecodeMouseReport(layout, xOnly, sizeof(xOnly), values), "X-only report decodes");
  checkEqual(values.x, 300, "X-only: X = +300");
  checkEqual(values.y, 0, "X-only: Y stays 0");
  checkEqual(values.wheel, 0, "X-only: wheel stays 0");

  // A truncated transfer cannot be decoded as this report.
  const uint8_t shortReport[] = {0x00, 0x00, 0x0a, 0x00};
  check(!espUsbHostDecodeMouseReport(layout, shortReport, sizeof(shortReport), values),
        "report too short for X and Y is rejected");
}

// A composite device: keyboard on report ID 1, mouse on report ID 2. The bit
// offsets of the mouse fields are relative to the report body, after the ID.
void testReportIdDescriptor()
{
  const uint8_t descriptor[] = {
      0x05, 0x01,       // Usage Page (Generic Desktop)
      0x09, 0x06,       // Usage (Keyboard)
      0xa1, 0x01,       // Collection (Application)
      0x85, 0x01,       //   Report ID (1)
      0x05, 0x07,       //   Usage Page (Keyboard)
      0x19, 0xe0,       //   Usage Minimum (0xE0)
      0x29, 0xe7,       //   Usage Maximum (0xE7)
      0x15, 0x00,       //   Logical Minimum (0)
      0x25, 0x01,       //   Logical Maximum (1)
      0x75, 0x01,       //   Report Size (1)
      0x95, 0x08,       //   Report Count (8)
      0x81, 0x02,       //   Input (Data,Var,Abs)
      0x95, 0x06,       //   Report Count (6)
      0x75, 0x08,       //   Report Size (8)
      0x15, 0x00,       //   Logical Minimum (0)
      0x26, 0xff, 0x00, //   Logical Maximum (255)
      0x19, 0x00,       //   Usage Minimum (0)
      0x29, 0xff,       //   Usage Maximum (255)
      0x81, 0x00,       //   Input (Data,Array,Abs)
      0xc0,             // End Collection
      0x05, 0x01,       // Usage Page (Generic Desktop)
      0x09, 0x02,       // Usage (Mouse)
      0xa1, 0x01,       // Collection (Application)
      0x85, 0x02,       //   Report ID (2)
      0x09, 0x01,       //   Usage (Pointer)
      0xa1, 0x00,       //   Collection (Physical)
      0x05, 0x09,       //     Usage Page (Button)
      0x19, 0x01,       //     Usage Minimum (1)
      0x29, 0x05,       //     Usage Maximum (5)
      0x15, 0x00,       //     Logical Minimum (0)
      0x25, 0x01,       //     Logical Maximum (1)
      0x95, 0x05,       //     Report Count (5)
      0x75, 0x01,       //     Report Size (1)
      0x81, 0x02,       //     Input (Data,Var,Abs)
      0x95, 0x01,       //     Report Count (1)
      0x75, 0x03,       //     Report Size (3)
      0x81, 0x03,       //     Input (Const,Var,Abs)
      0x05, 0x01,       //     Usage Page (Generic Desktop)
      0x09, 0x30,       //     Usage (X)
      0x09, 0x31,       //     Usage (Y)
      0x09, 0x38,       //     Usage (Wheel)
      0x15, 0x81,       //     Logical Minimum (-127)
      0x25, 0x7f,       //     Logical Maximum (127)
      0x75, 0x08,       //     Report Size (8)
      0x95, 0x03,       //     Report Count (3)
      0x81, 0x06,       //     Input (Data,Var,Rel)
      0xc0,             //   End Collection
      0xc0,             // End Collection
  };

  EspUsbHostMouseReportLayout layout;
  check(espUsbHostParseMouseReportLayout(descriptor, sizeof(descriptor), layout),
        "composite descriptor parses");
  checkEqual(layout.reportId, 2, "mouse is report ID 2");
  checkEqual(layout.buttonCount, 5, "5 buttons");
  checkEqual(layout.x.bitOffset, 8, "X at bit 8 of the body");
  checkEqual(layout.y.bitOffset, 16, "Y at bit 16 of the body");
  checkEqual(layout.wheel.bitOffset, 24, "wheel at bit 24 of the body");

  const uint8_t report[] = {0x02, 0x10, 0x03, 0xfd, 0x00};
  EspUsbHostMouseReportValues values;
  check(espUsbHostDecodeMouseReport(layout, report, sizeof(report), values), "ID 2 report decodes");
  checkEqual(values.buttons, 0x10, "button 5 held");
  checkEqual(values.x, 3, "X = +3");
  checkEqual(values.y, -3, "Y = -3");

  // The keyboard report must not be decoded as a mouse report.
  const uint8_t keyboardReport[] = {0x01, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
  check(!espUsbHostDecodeMouseReport(layout, keyboardReport, sizeof(keyboardReport), values),
        "a report with another ID is rejected");
}

// Generic Desktop X / Y also appear in joystick and gamepad collections, which
// must not be mistaken for the mouse.
void testJoystickIsNotAMouse()
{
  const uint8_t descriptor[] = {
      0x05, 0x01,       // Usage Page (Generic Desktop)
      0x09, 0x04,       // Usage (Joystick)
      0xa1, 0x01,       // Collection (Application)
      0x09, 0x01,       //   Usage (Pointer)
      0xa1, 0x00,       //   Collection (Physical)
      0x09, 0x30,       //     Usage (X)
      0x09, 0x31,       //     Usage (Y)
      0x15, 0x00,       //     Logical Minimum (0)
      0x26, 0xff, 0x00, //     Logical Maximum (255)
      0x75, 0x08,       //     Report Size (8)
      0x95, 0x02,       //     Report Count (2)
      0x81, 0x02,       //     Input (Data,Var,Abs)
      0xc0,             //   End Collection
      0xc0,             // End Collection
  };

  EspUsbHostMouseReportLayout layout;
  check(!espUsbHostParseMouseReportLayout(descriptor, sizeof(descriptor), layout),
        "a joystick is not taken for a mouse");
  check(!layout.valid, "joystick layout is not valid");
}

// A mouse that packs 12-bit axes across byte boundaries, and declares its
// globals through Push / Pop.
void testPackedAxesAndPush()
{
  const uint8_t descriptor[] = {
      0x05, 0x01,       // Usage Page (Generic Desktop)
      0x09, 0x02,       // Usage (Mouse)
      0xa1, 0x01,       // Collection (Application)
      0x05, 0x09,       //   Usage Page (Button)
      0x19, 0x01,       //   Usage Minimum (1)
      0x29, 0x08,       //   Usage Maximum (8)
      0x15, 0x00,       //   Logical Minimum (0)
      0x25, 0x01,       //   Logical Maximum (1)
      0x95, 0x08,       //   Report Count (8)
      0x75, 0x01,       //   Report Size (1)
      0x81, 0x02,       //   Input (Data,Var,Abs)
      0xa4,             //   Push
      0x05, 0x01,       //   Usage Page (Generic Desktop)
      0x16, 0x01, 0xf8, //   Logical Minimum (-2047)
      0x26, 0xff, 0x07, //   Logical Maximum (2047)
      0x75, 0x0c,       //   Report Size (12)
      0x95, 0x02,       //   Report Count (2)
      0x09, 0x30,       //   Usage (X)
      0x09, 0x31,       //   Usage (Y)
      0x81, 0x06,       //   Input (Data,Var,Rel)
      0xb4,             //   Pop
      0xc0,             // End Collection
  };

  EspUsbHostMouseReportLayout layout;
  check(espUsbHostParseMouseReportLayout(descriptor, sizeof(descriptor), layout),
        "packed-axis descriptor parses");
  checkEqual(layout.x.bitOffset, 8, "X at bit 8");
  checkEqual(layout.x.bitSize, 12, "X is 12-bit");
  checkEqual(layout.y.bitOffset, 20, "Y at bit 20");
  checkEqual(layout.reportBits, 32, "report is 4 bytes");
  check(!layout.wheel.present, "no wheel declared");

  // buttons = 1, X = -1 (0xfff), Y = +1
  const uint8_t report[] = {0x01, 0xff, 0x1f, 0x00};
  EspUsbHostMouseReportValues values;
  check(espUsbHostDecodeMouseReport(layout, report, sizeof(report), values), "packed report decodes");
  checkEqual(values.buttons, 0x01, "button 1 held");
  checkEqual(values.x, -1, "12-bit X sign extends to -1");
  checkEqual(values.y, 1, "12-bit Y = +1");
}

void testRejects()
{
  EspUsbHostMouseReportLayout layout;
  check(!espUsbHostParseMouseReportLayout(nullptr, 8, layout), "null descriptor is rejected");
  check(!espUsbHostParseMouseReportLayout(kBootMouseDescriptor, 0, layout), "empty descriptor is rejected");

  // Truncated in the middle of an item: the walk stops, and what was learned so
  // far is not enough to call it a mouse.
  check(!espUsbHostParseMouseReportLayout(kBootMouseDescriptor, 10, layout),
        "truncated descriptor is rejected");

  // A mouse collection with X but no Y is not usable.
  const uint8_t xOnlyDescriptor[] = {
      0x05, 0x01, // Usage Page (Generic Desktop)
      0x09, 0x02, // Usage (Mouse)
      0xa1, 0x01, // Collection (Application)
      0x09, 0x30, //   Usage (X)
      0x15, 0x81, //   Logical Minimum (-127)
      0x25, 0x7f, //   Logical Maximum (127)
      0x75, 0x08, //   Report Size (8)
      0x95, 0x01, //   Report Count (1)
      0x81, 0x06, //   Input (Data,Var,Rel)
      0xc0,       // End Collection
  };
  check(!espUsbHostParseMouseReportLayout(xOnlyDescriptor, sizeof(xOnlyDescriptor), layout),
        "a collection without Y is rejected");

  EspUsbHostMouseReportValues values;
  EspUsbHostMouseReportLayout invalid;
  const uint8_t report[] = {0x00, 0x00, 0x00, 0x00};
  check(!espUsbHostDecodeMouseReport(invalid, report, sizeof(report), values),
        "decoding with an invalid layout fails");
  check(espUsbHostParseMouseReportLayout(kBootMouseDescriptor, sizeof(kBootMouseDescriptor), layout),
        "boot descriptor re-parses");
  check(!espUsbHostDecodeMouseReport(layout, nullptr, 4, values), "null report is rejected");
  check(!espUsbHostDecodeMouseReport(layout, report, 0, values), "empty report is rejected");
}

} // namespace

int main()
{
  testBootMouseDescriptor();
  testGamingMouseDescriptor();
  testReportIdDescriptor();
  testJoystickIsNotAMouse();
  testPackedAxesAndPush();
  testRejects();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all mouse layout checks passed\n");
  return 0;
}
