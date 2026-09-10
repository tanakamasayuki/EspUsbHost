#include "EspUsbHost.h"

EspUsbHost usb;

// The USB host is not started in setup(). The peer board is flashed after this
// sketch has booted, so a host started here observes esptool resetting the peer
// and records the resulting enumerations as errors. The test starts it once the
// peer is in place, and stops it again in the fixture teardown so the board is
// not left hosting USB after the run.
//
// Q / G / H rather than lower case: every lower-case letter is already a test
// command in one sketch or another, so the lifecycle commands get their own
// range instead of colliding per sketch.
static bool hostStarted = false;

static void startHost()
{
    if (hostStarted)
    {
        return; // idempotent
    }
    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
        return;
    }
    hostStarted = true;
    // Nothing is printed here on purpose: an answer would have to be consumed by
    // an expect() in the fixture, which would advance the reader past the
    // enumeration output that follows and that the tests read.
}

static void stopHost()
{
    usb.end();
    hostStarted = false;
    Serial.println("HOST_STATE idle devices=0");
}

// Answered whenever it is asked, so a test can wait for enumeration by polling
// this rather than by waiting for a connect line that is printed once.
static bool handleLifecycle(char command)
{
    if (command == 'Q')
    {
        Serial.printf("HOST_STATE %s devices=%u\n",
                      hostStarted ? "running" : "idle",
                      static_cast<unsigned>(usb.deviceCount()));
        return true;
    }
    if (command == 'G')
    {
        startHost();
        return true;
    }
    if (command == 'H')
    {
        stopHost();
        return true;
    }
    return false;
}

static uint32_t audioBytes = 0;
static bool audioReported = false;
static uint8_t audioInputAddress = 0;
static uint8_t audioOutputAddress = 0;
static int16_t outputSamples[480];

void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
                              Serial.printf("DEVICE_CONNECTED addr=%u portId=0x%02x class=0x%02x\n",
                                            device.address,
                                            device.portId,
                                            device.deviceClass);
                              if (usb.audioInputReady(device.address))
                              {
                                  audioInputAddress = device.address;
                                  Serial.printf("AUDIO_IN_READY addr=%u\n", device.address);
                              }
                              if (usb.audioOutputReady(device.address))
                              {
                                  audioOutputAddress = device.address;
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

    Serial.println("HOST_STATE idle devices=0");
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
        if (handleLifecycle(command))
        {
            return;
        }
        if (command == 'r')
        {
            audioBytes = 0;
            audioReported = false;
            Serial.println("AUDIO_RESET");
        }
        else if (command == 'a')
        {
            Serial.printf("AUDIO_OUT_START %u\n", usb.audioOutputStart(1, 16, 48000, audioOutputAddress) ? 1 : 0);
        }
        else if (command == 'i')
        {
            Serial.printf("AUDIO_IN_START %u\n", usb.audioInputStart(1, 16, 48000, audioInputAddress) ? 1 : 0);
        }
        else if (command == 's')
        {
            uint32_t sent = 0;
            fillOutputSamples();
            if (usb.audioSend(reinterpret_cast<const uint8_t *>(outputSamples), sizeof(outputSamples), audioOutputAddress))
            {
                sent = sizeof(outputSamples);
            }
            Serial.printf("AUDIO_TX %lu\n", static_cast<unsigned long>(sent));
        }
    }
    delay(1);
}
