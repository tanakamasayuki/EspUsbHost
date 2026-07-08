#include "EspUsbHost.h"

// NKRO (N-key rollover) keyboard host. A boot keyboard reports at most 6 keys at
// once; an NKRO keyboard sends a bitmap instead, so any number of keys can be
// held simultaneously. EspUsbHost decodes both automatically from the HID report
// descriptor — no configuration needed — and delivers the same onKeyboard()
// press/release events either way. This sketch just reports how many keys are
// held at the same time, which is what proves NKRO is working.
//
// Pairs with the sibling EspUsbDevice "KeyboardNKRO" example (or any NKRO USB
// keyboard). Use keyboardUsesBitmapReport() to see which format was detected.

EspUsbHost usb;

static int pressedCount = 0; // keys currently held
static int maxSimultaneous = 0;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("Keyboard connected: %04x:%04x nkro-bitmap=%u\n",
                                        device.vid, device.pid,
                                        usb.keyboardUsesBitmapReport(device.address) ? 1 : 0);
                        });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed)
                   {
                     pressedCount++;
                     if (pressedCount > maxSimultaneous)
                     {
                       maxSimultaneous = pressedCount;
                     }
                     Serial.printf("press   keycode=0x%02x ascii=%c held=%d (max=%d)\n",
                                   event.keycode,
                                   (event.ascii >= 0x20 && event.ascii < 0x7f) ? (char)event.ascii : ' ',
                                   pressedCount, maxSimultaneous);
                   }
                   else
                   {
                     if (pressedCount > 0)
                     {
                       pressedCount--;
                     }
                     Serial.printf("release keycode=0x%02x held=%d\n", event.keycode, pressedCount);
                   }
                 });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
