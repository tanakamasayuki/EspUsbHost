#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                  {
                      Serial.printf("GAMEPAD report=");
                      for (size_t i = 0; i < event.reportLength; i++)
                      {
                          Serial.printf("%02x", event.reportData[i]);
                          if (i + 1 < event.reportLength)
                          {
                              Serial.print(" ");
                          }
                      }
                      Serial.printf(" fields=%u", (unsigned)event.fieldCount);
                      Serial.println(); });
    usb.addGamepadListener([](const EspUsbHostGamepadEvent &event)
                           { Serial.printf("GAMEPAD_LISTENER length=%u fields=%u\n",
                                           static_cast<unsigned>(event.reportLength),
                                           static_cast<unsigned>(event.fieldCount)); });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    delay(1);
}
