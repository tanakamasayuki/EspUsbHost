#include <Arduino.h>

#if ARDUINO_USB_MODE
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBAudioCard.h"

USBAudioCard AudioCard(48000, UAC_BPS_16, UAC_SPK_NONE, UAC_MIC_MONO);

static int16_t samples[48];

static void audioEventCallback(void *, esp_event_base_t eventBase, int32_t eventId, void *eventData)
{
    if (eventBase != ARDUINO_USB_AUDIO_CARD_EVENTS)
    {
        return;
    }
    arduino_usb_audio_card_event_data_t *data = static_cast<arduino_usb_audio_card_event_data_t *>(eventData);
    if (eventId == ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT)
    {
        Serial.printf("AUDIO_INTERFACE %s %u\n",
                      data->interface_enable.interface == UAC_INTERFACE_MIC ? "MIC" : "SPK",
                      data->interface_enable.enable ? 1 : 0);
    }
}

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
    AudioCard.onEvent(audioEventCallback);
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
