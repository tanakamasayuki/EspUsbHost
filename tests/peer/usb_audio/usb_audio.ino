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
static uint32_t outputNext = 0;
static uint32_t inputSequenceErrors = 0;
static uint16_t inputPrevious = 0;
static bool inputHavePrevious = false;
static bool startOutputFromCallback = false;
static portMUX_TYPE audioTestMux = portMUX_INITIALIZER_UNLOCKED;

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
                        portENTER_CRITICAL(&audioTestMux);
                        audioBytes += audio.length;
                        for (size_t i = 0; i + 1 < audio.length; i += 2)
                        {
                            const uint16_t value = audio.data[i] | (static_cast<uint16_t>(audio.data[i + 1]) << 8);
                            if (inputHavePrevious && value != static_cast<uint16_t>(inputPrevious + 197))
                            {
                                inputSequenceErrors++;
                            }
                            inputPrevious = value;
                            inputHavePrevious = true;
                        }
                        const bool startOutput = startOutputFromCallback;
                        startOutputFromCallback = false;
                        portEXIT_CRITICAL(&audioTestMux);
                        if (startOutput)
                        {
                            Serial.printf("AUDIO_OUT_START %u callback=1\n",
                                          usb.audioOutputStart(1, 16, 32000, audioOutputAddress) ? 1 : 0);
                        }
                        if (!audioReported && audioBytes >= 96)
                        {
                            audioReported = true;
                            Serial.printf("AUDIO_RX addr=%u iface=%u total=%lu last=%u\n",
                                          audio.address,
                                          audio.interfaceNumber,
                                          static_cast<unsigned long>(audioBytes),
                                          static_cast<unsigned>(audio.length));
                        } });

    usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                             {
        // A finite, nonzero pattern followed by the library's silence padding.
        // The peer sums samples, so losing any source sample changes the sum.
        if (outputNext == 0) return;
        size_t frames = 0;
        while (frames < request.frameCount && outputNext <= 4096)
        {
            const uint16_t value = outputNext++;
            request.data[frames * 2] = value & 0xff;
            request.data[frames * 2 + 1] = value >> 8;
            frames++;
        }
        request.writtenFrames = frames;
    });

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
            portENTER_CRITICAL(&audioTestMux);
            audioBytes = 0;
            audioReported = false;
            inputHavePrevious = false;
            inputSequenceErrors = 0;
            portEXIT_CRITICAL(&audioTestMux);
            Serial.println("AUDIO_RESET");
        }
        else if (command == 'a')
        {
            Serial.printf("AUDIO_OUT_START %u\n", usb.audioOutputStart(1, 16, 32000, audioOutputAddress) ? 1 : 0);
        }
        else if (command == 'i')
        {
            Serial.printf("AUDIO_IN_START %u\n", usb.audioInputStart(1, 16, 32000, audioInputAddress) ? 1 : 0);
        }
        else if (command == 't')
        {
            const bool stopped = usb.audioInputStop(audioInputAddress);
            Serial.printf("AUDIO_IN_STOP %u running=%u\n", stopped ? 1 : 0,
                          usb.audioInputRunning(audioInputAddress) ? 1 : 0);
        }
        else if (command == 'q')
        {
            portENTER_CRITICAL(&audioTestMux);
            const uint32_t bytes = audioBytes;
            const uint32_t errors = inputSequenceErrors;
            portEXIT_CRITICAL(&audioTestMux);
            Serial.printf("AUDIO_CAPTURE bytes=%lu sequence_errors=%lu running=%u\n",
                          static_cast<unsigned long>(bytes), static_cast<unsigned long>(errors),
                          usb.audioInputRunning(audioInputAddress) ? 1 : 0);
        }
        else if (command == 'b')
        {
            outputNext = 1; // Issued before playback starts, while callbacks are idle.
            Serial.printf("AUDIO_OUT_START %u\n", usb.audioOutputStart(1, 16, 32000, audioOutputAddress) ? 1 : 0);
        }
        else if (command == 'o')
        {
            usb.audioOutputStop(audioOutputAddress);
            Serial.println("AUDIO_OUT_STOP");
        }
        else if (command == 'c')
        {
            outputNext = 1;
            portENTER_CRITICAL(&audioTestMux);
            startOutputFromCallback = true;
            portEXIT_CRITICAL(&audioTestMux);
            Serial.println("AUDIO_OUT_CALLBACK_ARMED");
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
