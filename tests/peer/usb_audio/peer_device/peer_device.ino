#include <Arduino.h>

#if ARDUINO_USB_MODE
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBAudioCard.h"

USBAudioCard AudioCard(48000, UAC_BPS_16, UAC_SPK_NONE, UAC_MIC_MONO);

static int16_t samples[48];

static void fillSamples()
{
    static int16_t value = 0;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
    {
        samples[i] = value;
        value += 257;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    AudioCard.begin();
    USB.begin();
    Serial.println("AUDIO_DEVICE_READY");
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'a')
        {
            uint32_t accepted = 0;
            const uint32_t start = millis();
            while (millis() - start < 500)
            {
                fillSamples();
                accepted += AudioCard.write(samples, sizeof(samples));
                delay(1);
            }
            Serial.printf("DEVICE_TX_AUDIO %lu\n", static_cast<unsigned long>(accepted));
        }
    }
    delay(1);
}

#endif
