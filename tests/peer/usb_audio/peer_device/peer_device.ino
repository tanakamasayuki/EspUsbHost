#include <Arduino.h>

#if ARDUINO_USB_MODE
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBAudioCard.h"

USBAudioCard AudioCard(32000, UAC_BPS_16, UAC_SPK_MONO, UAC_MIC_MONO);

static uint32_t receivedAudioBytes = 0;
static bool receivedAudioReported = false;
static int16_t micSamples[32];
static int16_t micValue = 0;
static uint32_t receivedAudioSum = 0;
static portMUX_TYPE audioTestMux = portMUX_INITIALIZER_UNLOCKED;

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

static void audioDataCallback(void *buffer, uint16_t length)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
    portENTER_CRITICAL(&audioTestMux);
    receivedAudioBytes += length;
    for (uint16_t i = 0; i + 1 < length; i += 2)
    {
        receivedAudioSum += bytes[i] | (static_cast<uint16_t>(bytes[i + 1]) << 8);
    }
    portEXIT_CRITICAL(&audioTestMux);
    if (!receivedAudioReported && receivedAudioBytes >= 96)
    {
        receivedAudioReported = true;
        Serial.printf("DEVICE_RX_AUDIO %lu\n", static_cast<unsigned long>(receivedAudioBytes));
    }
}

static void fillMicSamples()
{
    for (size_t i = 0; i < sizeof(micSamples) / sizeof(micSamples[0]); i++)
    {
        micSamples[i] = micValue;
        micValue += 197;
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
            portENTER_CRITICAL(&audioTestMux);
            receivedAudioBytes = 0;
            receivedAudioSum = 0;
            receivedAudioReported = false;
            portEXIT_CRITICAL(&audioTestMux);
            Serial.println("DEVICE_AUDIO_RESET");
        }
        else if (command == 'q')
        {
            portENTER_CRITICAL(&audioTestMux);
            const uint32_t bytes = receivedAudioBytes;
            const uint32_t sum = receivedAudioSum;
            portEXIT_CRITICAL(&audioTestMux);
            Serial.printf("DEVICE_AUDIO_RX bytes=%lu sum=%lu\n",
                          static_cast<unsigned long>(bytes), static_cast<unsigned long>(sum));
        }
        else if (command == 'M')
        {
            uint32_t total = 0;
            uint16_t offset = sizeof(micSamples);
            const uint32_t started = millis();
            while (millis() - started < 2000)
            {
                if (offset == sizeof(micSamples))
                {
                    fillMicSamples();
                    offset = 0;
                }
                const uint16_t written = AudioCard.write(reinterpret_cast<const uint8_t *>(micSamples) + offset,
                                                         sizeof(micSamples) - offset);
                offset += written;
                total += written;
                // TinyUSB's microphone FIFO is overwritable: write() can report
                // success while evicting older PCM. Pace the source at 1 ms;
                // a tight producer loop would test FIFO overwrite, not capture.
                delay(1);
            }
            Serial.printf("DEVICE_AUDIO_STREAM bytes=%lu ms=%lu\n",
                          static_cast<unsigned long>(total), static_cast<unsigned long>(millis() - started));
        }
        else if (command == 'm')
        {
            uint32_t total = 0;
            for (uint8_t i = 0; i < 20; i++)
            {
                fillMicSamples();
                total += AudioCard.write(micSamples, sizeof(micSamples));
                delay(1);
            }
            Serial.printf("DEVICE_TX_AUDIO %lu\n", static_cast<unsigned long>(total));
        }
    }
    delay(1);
}

#endif
