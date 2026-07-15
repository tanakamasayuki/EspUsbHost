#include "EspUsbHost.h"

// Host side of the composite-HID LED peer test. The peer is an EspUsbDevice
// composite (keyboard + consumer control + mouse merged into one interface with
// report IDs, no boot subclass/protocol), so the keyboard is only recognizable
// from the HID report descriptor. setKeyboardLeds() must still reach it, with
// the LED Set_Report carrying the keyboard's report ID.

EspUsbHost usb;

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
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x\n", device.vid, device.pid);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             Serial.println("HOST_DISCONNECTED");
                           });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     Serial.printf("KEY %c\n", (char)event.ascii);
                   }
                 });

  usb.onConsumerControl([](const EspUsbHostConsumerControlEvent &event)
                        {
                          Serial.printf("CONSUMER usage=0x%04x pressed=%u\n",
                                        event.usage,
                                        event.pressed ? 1 : 0);
                        });

  usb.onMouse([](const EspUsbHostMouseEvent &event)
              {
                if (event.moved)
                {
                  Serial.printf("MOUSE x=%d y=%d\n", event.x, event.y);
                }
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
    if (command == 'n')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(true, false, false) ? 1 : 0);
    }
    else if (command == 'c')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, true, false) ? 1 : 0);
    }
    else if (command == 's')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, true) ? 1 : 0);
    }
    else if (command == 'o')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, false) ? 1 : 0);
    }
  }
  delay(1);
}
