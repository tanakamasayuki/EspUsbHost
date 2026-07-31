#include "EspUsbHost.h"

EspUsbHost usb;
static volatile bool verboseKeyboard = false;
static volatile bool listenerTestActive = false;
static volatile bool listenerMutated = false;
static EspUsbHostListenerId listenerIds[4] = {};
static EspUsbHostListenerId replacementListenerId = ESP_USB_HOST_INVALID_LISTENER_ID;
static EspUsbHostListenerId stateListenerId = ESP_USB_HOST_INVALID_LISTENER_ID;

static void clearListenerTest()
{
    for (size_t i = 0; i < 4; i++)
    {
        if (listenerIds[i] != ESP_USB_HOST_INVALID_LISTENER_ID)
        {
            usb.removeListener(listenerIds[i]);
            listenerIds[i] = ESP_USB_HOST_INVALID_LISTENER_ID;
        }
    }
    if (replacementListenerId != ESP_USB_HOST_INVALID_LISTENER_ID)
    {
        usb.removeListener(replacementListenerId);
        replacementListenerId = ESP_USB_HOST_INVALID_LISTENER_ID;
    }
    if (stateListenerId != ESP_USB_HOST_INVALID_LISTENER_ID)
    {
        usb.removeListener(stateListenerId);
        stateListenerId = ESP_USB_HOST_INVALID_LISTENER_ID;
    }
    listenerTestActive = false;
    listenerMutated = false;
}

void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
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
      if (listenerTestActive && event.pressed)
      {
          Serial.println("LISTENER PRIMARY");
      }
      if (event.pressed && event.ascii && !verboseKeyboard && !listenerTestActive)
      {
          Serial.print((char)event.ascii);
      }
      if (verboseKeyboard && event.pressed)
      {
          Serial.printf("RAW_KEY keycode=0x%02x ascii=0x%02x modifiers=0x%02x unicode=0x%04x\n",
                        event.keycode,
                        event.ascii,
                        event.modifiers,
                        event.unicode);
          if (event.ascii)
          {
              Serial.printf("KEY %c\n", static_cast<char>(event.ascii));
          }
      } });

    usb.onKeyboardState([](const EspUsbHostKeyboardState &state)
                        {
      if (verboseKeyboard)
      {
          Serial.printf("KEY_STATE modifiers=0x%02x a_down=%u a_pressed=%u a_released=%u lctrl_down=%u lctrl_pressed=%u lctrl_released=%u\n",
                        state.modifiers,
                        state.isDown(0x04) ? 1 : 0,
                        state.wasPressed(0x04) ? 1 : 0,
                        state.wasReleased(0x04) ? 1 : 0,
                        state.isDown(0xe0) ? 1 : 0,
                        state.wasPressed(0xe0) ? 1 : 0,
                        state.wasReleased(0xe0) ? 1 : 0);
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
        else if (command == 'd')
        {
            verboseKeyboard = true;
            usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE);
            Serial.println("LAYOUT DE_DE");
        }
        else if (command == 'q')
        {
            verboseKeyboard = false;
            Serial.println("VERBOSE 0");
        }
        else if (command == 'l')
        {
            clearListenerTest();
            const bool emptyRejected = usb.addKeyboardListener(EspUsbHost::KeyboardCallback()) == ESP_USB_HOST_INVALID_LISTENER_ID;
            listenerIds[0] = usb.addKeyboardListener([](const EspUsbHostKeyboardEvent &event)
                                                     {
                if (!listenerTestActive || !event.pressed)
                {
                    return;
                }
                Serial.println("LISTENER 1");
                if (!listenerMutated)
                {
                    listenerMutated = true;
                    usb.removeListener(listenerIds[0]);
                    listenerIds[0] = ESP_USB_HOST_INVALID_LISTENER_ID;
                    replacementListenerId = usb.addKeyboardListener([](const EspUsbHostKeyboardEvent &nextEvent)
                                                                    {
                        if (listenerTestActive && nextEvent.pressed)
                        {
                            Serial.println("LISTENER 5");
                        } });
                } });
            listenerIds[1] = usb.addKeyboardListener([](const EspUsbHostKeyboardEvent &event)
                                                     {
                if (listenerTestActive && event.pressed)
                {
                    Serial.println("LISTENER 2");
                } });
            listenerIds[2] = usb.addKeyboardListener([count = 0](const EspUsbHostKeyboardEvent &event) mutable
                                                     {
                if (listenerTestActive && event.pressed)
                {
                    count++;
                    Serial.printf("LISTENER_STATE count=%u\n", static_cast<unsigned>(count));
                } });
            listenerIds[3] = usb.addKeyboardListener([](const EspUsbHostKeyboardEvent &) {});
            const bool allIdsValid = listenerIds[0] != ESP_USB_HOST_INVALID_LISTENER_ID &&
                                     listenerIds[1] != ESP_USB_HOST_INVALID_LISTENER_ID &&
                                     listenerIds[2] != ESP_USB_HOST_INVALID_LISTENER_ID &&
                                     listenerIds[3] != ESP_USB_HOST_INVALID_LISTENER_ID;
            const bool capacityRejected = usb.addKeyboardListener([](const EspUsbHostKeyboardEvent &) {}) == ESP_USB_HOST_INVALID_LISTENER_ID;
            const bool invalidRemoveRejected = !usb.removeListener(ESP_USB_HOST_INVALID_LISTENER_ID) &&
                                               !usb.removeListener(0xffffffffu);
            stateListenerId = usb.addKeyboardStateListener([](const EspUsbHostKeyboardState &state)
                                                           {
                if (listenerTestActive)
                {
                    Serial.printf("STATE_LISTENER a_down=%u changed=%u\n",
                                  state.isDown(0x04) ? 1 : 0,
                                  state.changedBitmap[0] ? 1 : 0);
                } });
            listenerTestActive = true;
            Serial.printf("LISTENER_SETUP empty=%u ids=%u capacity=%u invalid_remove=%u state=%u max=%u\n",
                          emptyRejected ? 1 : 0,
                          allIdsValid ? 1 : 0,
                          capacityRejected ? 1 : 0,
                          invalidRemoveRejected ? 1 : 0,
                          stateListenerId != ESP_USB_HOST_INVALID_LISTENER_ID ? 1 : 0,
                          static_cast<unsigned>(EspUsbHost::MaxListenersPerEvent));
        }
        else if (command == 'u')
        {
            const EspUsbHostListenerId removedId = listenerIds[1];
            const bool removed = usb.removeListener(removedId);
            listenerIds[1] = ESP_USB_HOST_INVALID_LISTENER_ID;
            const bool secondRemoveRejected = !usb.removeListener(removedId);
            Serial.printf("LISTENER_REMOVE removed=%u second=%u\n",
                          removed ? 1 : 0,
                          secondRemoveRejected ? 1 : 0);
        }
        else if (command == 'x')
        {
            clearListenerTest();
            Serial.println("LISTENER_CLEAR 1");
        }
    }
    delay(1);
}
