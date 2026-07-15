#include "EspUsbHost.h"

// Host side of the NKRO peer test. Counts how many keys the attached keyboard
// holds at the same time and reports the maximum, which is what distinguishes
// NKRO (bitmap, unlimited) from a boot keyboard (6-key limit).

EspUsbHost usb;

static volatile int pressedCount = 0;
static volatile int maxSimultaneous = 0;
static volatile uint8_t connectedAddress = 0;

void setup()
{
  // Event prints can burst faster than the default serial TX buffer
  // drains; enlarge it so lines are not truncated mid-flight.
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          connectedAddress = device.address;
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x\n", device.vid, device.pid);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             connectedAddress = 0;
                             pressedCount = 0;
                             Serial.println("HOST_DISCONNECTED");
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
                     Serial.printf("PRESS keycode=0x%02x n=%d\n", event.keycode, pressedCount);
                   }
                   else
                   {
                     if (pressedCount > 0)
                     {
                       pressedCount--;
                     }
                     Serial.printf("RELEASE keycode=0x%02x n=%d\n", event.keycode, pressedCount);
                   }
                 });

  usb.onKeyboardState([](const EspUsbHostKeyboardState &state)
                      {
                        int down = 0;
                        for (uint16_t usage = 0; usage <= 0xff; usage++)
                        {
                          if (state.isDown(static_cast<uint8_t>(usage)))
                          {
                            down++;
                          }
                        }
                        Serial.printf("STATE down=%d\n", down);
                      });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      Serial.printf("NKRO bitmap=%u\n", usb.keyboardUsesBitmapReport(connectedAddress) ? 1 : 0);
    }
    else if (command == 'r')
    {
      pressedCount = 0;
      maxSimultaneous = 0;
      Serial.println("RESET");
    }
    else if (command == 'm')
    {
      Serial.printf("MAX n=%d\n", maxSimultaneous);
    }
    else if (command == 'l')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, true, false) ? 1 : 0);
    }
    else if (command == 'o')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, false) ? 1 : 0);
    }
  }
  delay(1);
}
