#include "EspUsbHost.h"

EspUsbHost usb;
static volatile bool verboseKeyboard = false;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onHIDInput([](const EspUsbHostHIDInput &input)
                   {
      size_t offset = 0;
      if (input.length >= 9 && input.data[0] == ESP_USB_HOST_HID_REPORT_ID_KEYBOARD)
      {
          offset = 1;
      }
      if (verboseKeyboard && input.subclass == 1 && input.protocol == 1 && input.length >= offset + 8)
      {
          Serial.printf("HID_INPUT modifier=0x%02x reserved=0x%02x key0=0x%02x len=%u\n",
                        input.data[offset],
                        input.data[offset + 1],
                        input.data[offset + 2],
                        static_cast<unsigned>(input.length - offset));
      } });

    usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                   {
      if (event.pressed && event.ascii && !verboseKeyboard)
      {
          Serial.print((char)event.ascii);
      }
      if (verboseKeyboard && event.pressed)
      {
          Serial.printf("RAW_KEY keycode=0x%02x ascii=0x%02x modifiers=0x%02x\n",
                        event.keycode,
                        event.ascii,
                        event.modifiers);
          if (event.ascii)
          {
              Serial.printf("KEY %c\n", static_cast<char>(event.ascii));
          }
      } });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
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
        else if (command == '0')
        {
            Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, false) ? 1 : 0);
        }
        else if (command == 'e')
        {
            verboseKeyboard = true;
            usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US);
            Serial.println("LAYOUT EN_US");
        }
        else if (command == 'j')
        {
            verboseKeyboard = true;
            usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP);
            Serial.println("LAYOUT JA_JP");
        }
        else if (command == 'q')
        {
            verboseKeyboard = false;
            Serial.println("VERBOSE 0");
        }
    }
    delay(1);
}
