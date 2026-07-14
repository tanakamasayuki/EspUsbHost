#include "EspUsbHost.h"

// NKRO (N-key rollover) keyboard host. A boot keyboard reports at most 6 keys at
// once; an NKRO keyboard sends a bitmap instead, so any number of keys can be
// held simultaneously. EspUsbHost decodes both automatically from the HID report
// descriptor — no configuration needed — and normalizes both formats for
// onKeyboardState(). This sketch reports every changed HID usage, including
// modifiers (0xE0-0xE7), and counts how many keys are held at the same time.
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

  usb.onKeyboardState([](const EspUsbHostKeyboardState &state)
                      {
                        pressedCount = 0;
                        for (uint16_t usage = 0; usage <= 0xff; usage++)
                        {
                          const uint8_t keycode = static_cast<uint8_t>(usage);
                          if (state.isDown(keycode))
                          {
                            pressedCount++;
                          }
                          if (state.wasPressed(keycode))
                          {
                            Serial.printf("press   keycode=0x%02x\n", keycode);
                          }
                          else if (state.wasReleased(keycode))
                          {
                            Serial.printf("release keycode=0x%02x\n", keycode);
                          }
                        }
                        if (pressedCount > maxSimultaneous)
                        {
                          maxSimultaneous = pressedCount;
                        }
                        Serial.printf("held=%d (max=%d) modifiers=0x%02x\n",
                                      pressedCount, maxSimultaneous, state.modifiers);
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
