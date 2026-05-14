#include <Arduino.h>

#if ARDUINO_USB_MODE
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBAudioCard.h"

USBAudioCard AudioCard(48000, UAC_BPS_16, UAC_SPK_MONO, UAC_MIC_NONE);

static uint32_t receivedAudioBytes = 0;
static bool receivedAudioReported = false;

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

static void audioDataCallback(void *, uint16_t length)
{
    receivedAudioBytes += length;
    if (!receivedAudioReported && receivedAudioBytes >= 96)
    {
        receivedAudioReported = true;
        Serial.printf("DEVICE_RX_AUDIO %lu\n", static_cast<unsigned long>(receivedAudioBytes));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);
    AudioCard.onEvent(audioEventCallback);
    AudioCard.onData(audioDataCallback);
    AudioCard.begin();
    USB.begin();
    Serial.println("AUDIO_DEVICE_READY");
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'r')
        {
            receivedAudioBytes = 0;
            receivedAudioReported = false;
            Serial.println("DEVICE_AUDIO_RESET");
        }
    }
    delay(1);
}

#endif
