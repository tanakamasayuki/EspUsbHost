#include "EspUsbHost.h"

EspUsbHost usb;

static uint32_t audioBytes = 0;
static bool audioReported = false;
static uint8_t audioAddress = 0;
static int16_t outputSamples[480];

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
                              Serial.printf("DEVICE_CONNECTED addr=%u portId=0x%02x class=0x%02x\n",
                                            device.address,
                                            device.portId,
                                            device.deviceClass);
                              if (usb.audioReady(device.address))
                              {
                                  Serial.printf("AUDIO_READY addr=%u\n", device.address);
                              }
                              if (usb.audioOutputReady(device.address))
                              {
                                  audioAddress = device.address;
                                  Serial.printf("AUDIO_OUT_READY addr=%u\n", device.address);
                              }
                              EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
                              const size_t interfaceCount = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
                              for (size_t i = 0; i < interfaceCount; i++)
                              {
                                  Serial.printf("INTERFACE number=%u alt=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u\n",
                                                interfaces[i].number,
                                                interfaces[i].alternate,
                                                interfaces[i].interfaceClass,
                                                interfaces[i].interfaceSubClass,
                                                interfaces[i].interfaceProtocol,
                                                interfaces[i].endpointCount);
                              }
                              EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
                              const size_t endpointCount = usb.getEndpoints(device.address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);
                              for (size_t i = 0; i < endpointCount; i++)
                              {
                                  Serial.printf("ENDPOINT iface=%u ep=0x%02x attrs=0x%02x max=%u interval=%u\n",
                                                endpoints[i].interfaceNumber,
                                                endpoints[i].address,
                                                endpoints[i].attributes,
                                                endpoints[i].maxPacketSize,
                                                endpoints[i].interval);
                              }
                              EspUsbHostAudioStreamInfo audioStreams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                              const size_t audioStreamCount = usb.getAudioStreams(device.address, audioStreams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                              for (size_t i = 0; i < audioStreamCount; i++)
                              {
                                  Serial.printf("AUDIO_STREAM iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu rates=%u first=%lu min=%lu max=%lu maxPacket=%u interval=%u\n",
                                                audioStreams[i].interfaceNumber,
                                                audioStreams[i].alternate,
                                                audioStreams[i].endpointAddress,
                                                audioStreams[i].input ? "IN" : "OUT",
                                                audioStreams[i].channels,
                                                audioStreams[i].bytesPerSample,
                                                audioStreams[i].bitsPerSample,
                                                static_cast<unsigned long>(audioStreams[i].sampleRate),
                                                audioStreams[i].sampleRateCount,
                                                static_cast<unsigned long>(audioStreams[i].sampleRateCount > 0 ? audioStreams[i].sampleRates[0] : 0),
                                                static_cast<unsigned long>(audioStreams[i].sampleRateMin),
                                                static_cast<unsigned long>(audioStreams[i].sampleRateMax),
                                                audioStreams[i].maxPacketSize,
                                                audioStreams[i].interval);
                              } });

    usb.onAudioData([](const EspUsbHostAudioData &audio)
                    {
                        audioBytes += audio.length;
                        if (!audioReported && audioBytes >= 96)
                        {
                            audioReported = true;
                            Serial.printf("AUDIO_RX addr=%u iface=%u total=%lu last=%u\n",
                                          audio.address,
                                          audio.interfaceNumber,
                                          static_cast<unsigned long>(audioBytes),
                                          static_cast<unsigned>(audio.length));
                        } });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

static void fillOutputSamples()
{
    static int16_t value = 0;
    for (size_t i = 0; i < sizeof(outputSamples) / sizeof(outputSamples[0]); i++)
    {
        outputSamples[i] = value;
        value += 257;
    }
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'r')
        {
            audioBytes = 0;
            audioReported = false;
            Serial.println("AUDIO_RESET");
        }
        else if (command == 's')
        {
            uint32_t sent = 0;
            fillOutputSamples();
            if (usb.audioSend(reinterpret_cast<const uint8_t *>(outputSamples), sizeof(outputSamples), audioAddress))
            {
                sent = sizeof(outputSamples);
            }
            Serial.printf("AUDIO_TX %lu\n", static_cast<unsigned long>(sent));
        }
    }
    delay(1);
}
