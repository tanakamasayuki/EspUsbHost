// UAC2 audio peer for tests/peer/usb_audio_uac2.
//
// The Arduino core's USBAudioCard (used by tests/peer/usb_audio) is UAC1 only, so
// the UAC2 peer is built with the sibling EspUsbDevice library, which emits UAC2
// descriptors and Clock Source / Feature Unit class requests when the protocol is
// selected explicitly. Commands mirror the UAC1 peer: 'r' resets the received byte
// count, 'm' sends a short capture burst. Playback bytes are only counted, not fed
// back into capture, so each direction is asserted on its own.

#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
EspUsbAudioCaptureStream &capture = audio.addCaptureStream();

static uint32_t receivedAudioBytes = 0;
static bool receivedAudioReported = false;
static int16_t micSamples[48];
static int16_t micValue = 0;

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

    playback.addFormat({48000, 1, 2, 16});
    capture.addFormat({48000, 1, 2, 16});

    EspUsbDeviceConfig config;
    config.vid = 0x303a;
    config.pid = 0x4024;
    config.manufacturer = "EspUsb";
    config.product = "EspUsbDevice UAC2 Headset";
    config.serialNumber = "espusb-uac2";

    if (!device.begin(config))
    {
        Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
        return;
    }
    Serial.println("AUDIO_DEVICE_READY");
}

void loop()
{
    uint8_t pcm[192];
    const size_t received = playback.read(pcm, sizeof(pcm));
    if (received)
    {
        receivedAudioBytes += received;
        if (!receivedAudioReported && receivedAudioBytes >= 96)
        {
            receivedAudioReported = true;
            Serial.printf("DEVICE_RX_AUDIO %lu\n", static_cast<unsigned long>(receivedAudioBytes));
        }
    }

    EspUsbAudioEvent event;
    while (audio.pollEvent(event))
    {
        if (event.type == EspUsbAudioEventType::StreamStateChanged)
        {
            Serial.printf("AUDIO_INTERFACE %s %u\n",
                          event.target == EspUsbAudioEventTarget::Playback ? "SPK" : "MIC",
                          event.enabled ? 1 : 0);
        }
        else if (event.type == EspUsbAudioEventType::SampleRateChanged)
        {
            Serial.printf("AUDIO_RATE %lu\n", static_cast<unsigned long>(event.sampleRate));
        }
    }

    if (Serial.available() > 0)
    {
        const char command = Serial.read();
        if (command == 'r')
        {
            receivedAudioBytes = 0;
            receivedAudioReported = false;
            Serial.println("DEVICE_AUDIO_RESET");
        }
        else if (command == 'm')
        {
            uint32_t total = 0;
            // A single write races the isochronous IN schedule, so send a short
            // burst at 1 ms intervals like the UAC1 peer does.
            for (uint8_t i = 0; i < 20; i++)
            {
                fillMicSamples();
                total += capture.write(micSamples, sizeof(micSamples));
                delay(1);
            }
            Serial.printf("DEVICE_TX_AUDIO %lu\n", static_cast<unsigned long>(total));
        }
    }
    delay(1);
}
