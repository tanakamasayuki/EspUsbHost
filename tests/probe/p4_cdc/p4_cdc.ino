#include <Arduino.h>

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("TEST_BEGIN p4_cdc_probe");
  Serial.printf("ARDUINO_USB_MODE=%d ARDUINO_USB_CDC_ON_BOOT=%d\n",
                ARDUINO_USB_MODE,
                ARDUINO_USB_CDC_ON_BOOT);
  Serial.println("CDC_PROBE_READY");
}

void loop()
{
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 1000)
  {
    lastPrint = millis();
    Serial.printf("CDC_PROBE_ALIVE ms=%lu\n", static_cast<unsigned long>(millis()));
  }

  while (Serial.available() > 0)
  {
    Serial.print("CDC_RX ");
    while (Serial.available() > 0)
    {
      Serial.write(Serial.read());
    }
    Serial.println();
  }

  delay(1);
}
