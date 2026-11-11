#include "EspUsbHost.h"

// Stress for the window inside end() where ESP-IDF has dropped its host
// library object but still has the USB interrupt installed. The peer attaches
// and detaches on its own schedule while this sketch cycles begin()/end(), so
// sooner or later an attach or detach lands inside that window.

EspUsbHost usb;

static volatile uint32_t connects = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &)
                        { connects++; });
  Serial.println("RACE_BOOT");
}

void loop()
{
  const int command = Serial.available() > 0 ? Serial.read() : -1;
  if (command == '?')
  {
    Serial.println("RACE_IDLE");
  }
  if (command == 'S')
  {
    const uint32_t cycles = 300;
    uint32_t beginFailures = 0;
    for (uint32_t i = 1; i <= cycles; i++)
    {
      if (!usb.begin())
      {
        beginFailures++;
        Serial.printf("RACE_BEGIN_FAILED cycle=%lu error=%s\n",
                      static_cast<unsigned long>(i), usb.lastErrorName());
      }
      delay(esp_random() % 400);
      usb.end();
      if (i % 25 == 0)
      {
        Serial.printf("RACE_CYCLE %lu connects=%lu begin_failures=%lu heap=%lu\n",
                      static_cast<unsigned long>(i),
                      static_cast<unsigned long>(connects),
                      static_cast<unsigned long>(beginFailures),
                      static_cast<unsigned long>(ESP.getFreeHeap()));
      }
    }
    Serial.printf("RACE_DONE cycles=%lu connects=%lu begin_failures=%lu\n",
                  static_cast<unsigned long>(cycles),
                  static_cast<unsigned long>(connects),
                  static_cast<unsigned long>(beginFailures));
  }
  delay(1);
}
