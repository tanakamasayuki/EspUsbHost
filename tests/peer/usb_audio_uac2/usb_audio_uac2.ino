// Host side of the UAC2 peer test. Mirrors tests/peer/usb_audio (UAC1) and adds
// the UAC2-specific facts: the parsed class revision, the Clock Source that
// carries the sample rate, and the Feature Unit volume range read through the
// UAC2 RANGE request.

#include "EspUsbHost.h"

EspUsbHost usb;

static uint32_t audioBytes = 0;
static bool audioReported = false;
static uint8_t audioInputAddress = 0;
static uint8_t audioOutputAddress = 0;
static uint8_t audioAddress = 0;
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
                              audioAddress = device.address;
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
                              EspUsbHostAudioFeatureUnitInfo units[ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS];
                              const size_t unitCount = usb.getAudioFeatureUnits(device.address, units, ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS);
                              for (size_t i = 0; i < unitCount; i++)
                              {
                                  Serial.printf("AUDIO_UNIT unit=%u source=%u channels=%u control_size=%u master=0x%lx proto=0x%02x mute=%u volume=%u\n",
                                                units[i].unitId,
                                                units[i].sourceId,
                                                units[i].channelCount,
                                                units[i].controlSize,
                                                static_cast<unsigned long>(units[i].masterControls),
                                                units[i].protocol,
                                                usb.audioHasMute(device.address, units[i].unitId) ? 1 : 0,
                                                usb.audioHasVolume(device.address, units[i].unitId) ? 1 : 0);
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

// UAC2 sample rates arrive from an asynchronous class request rather than from
// the descriptors, so the stream dump is a command instead of part of the
// connect callback.
static void printAudioStreams()
{
    EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
    const size_t streamCount = usb.getAudioStreams(audioAddress, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
    for (size_t i = 0; i < streamCount; i++)
    {
        Serial.printf("AUDIO_STREAM iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu rates=%u first=%lu min=%lu max=%lu maxPacket=%u interval=%u proto=0x%02x terminal=%u clock=%u startable=%u\n",
                      streams[i].interfaceNumber,
                      streams[i].alternate,
                      streams[i].endpointAddress,
                      streams[i].input ? "IN" : "OUT",
                      streams[i].channels,
                      streams[i].bytesPerSample,
                      streams[i].bitsPerSample,
                      static_cast<unsigned long>(streams[i].sampleRate),
                      streams[i].sampleRateCount,
                      static_cast<unsigned long>(streams[i].sampleRateCount > 0 ? streams[i].sampleRates[0] : 0),
                      static_cast<unsigned long>(streams[i].sampleRateMin),
                      static_cast<unsigned long>(streams[i].sampleRateMax),
                      streams[i].maxPacketSize,
                      streams[i].interval,
                      streams[i].protocol,
                      streams[i].terminalLink,
                      streams[i].clockSourceId,
                      streams[i].startable ? 1 : 0);
    }
    Serial.printf("AUDIO_STREAM_COUNT %u\n", static_cast<unsigned>(streamCount));
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
        else if (command == 'd')
        {
            printAudioStreams();
        }
        else if (command == 'a')
        {
            Serial.printf("AUDIO_OUT_START %u\n", usb.audioOutputStart(1, 16, 48000, audioOutputAddress) ? 1 : 0);
        }
        else if (command == 'A')
        {
            // Zero means "no preference": the library picks the best format the
            // device offers. Report what it resolved to so the test can assert it.
            const bool started = usb.audioOutputStart(0, 0, 0, audioOutputAddress);
            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
            const size_t count = usb.getAudioStreams(audioOutputAddress, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
            const EspUsbHostAudioStreamSelection best =
                espUsbHostSelectAudioOutputStream(streams, count);
            Serial.printf("AUDIO_OUT_AUTO started=%u channels=%u bits=%u rate=%lu\n",
                          started ? 1 : 0,
                          best ? streams[best.index].channels : 0,
                          best ? streams[best.index].bitsPerSample : 0,
                          static_cast<unsigned long>(best ? best.sampleRate : 0));
        }
        else if (command == 'i')
        {
            Serial.printf("AUDIO_IN_START %u\n", usb.audioInputStart(1, 16, 48000, audioInputAddress) ? 1 : 0);
        }
        else if (command == 'I')
        {
            const bool started = usb.audioInputStart(0, 0, 0, audioInputAddress);
            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
            const size_t count = usb.getAudioStreams(audioInputAddress, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
            const EspUsbHostAudioStreamSelection best =
                espUsbHostSelectAudioInputStream(streams, count);
            Serial.printf("AUDIO_IN_AUTO started=%u channels=%u bits=%u rate=%lu\n",
                          started ? 1 : 0,
                          best ? streams[best.index].channels : 0,
                          best ? streams[best.index].bitsPerSample : 0,
                          static_cast<unsigned long>(best ? best.sampleRate : 0));
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
        else if (command == 'f')
        {
            // Explicit feedback endpoint state. A UAC2 asynchronous playback
            // interface reports the rate it wants the host to send at; pacing is
            // the rate the OUT packets are actually filled for.
            Serial.printf("AUDIO_FEEDBACK has=%u rate=%lu updates=%lu rejects=%lu pacing=%lu\n",
                          usb.audioOutputHasFeedback(audioOutputAddress) ? 1 : 0,
                          static_cast<unsigned long>(usb.audioOutputFeedbackRate(audioOutputAddress)),
                          static_cast<unsigned long>(usb.audioOutputFeedbackUpdates(audioOutputAddress)),
                          static_cast<unsigned long>(usb.audioOutputFeedbackRejects(audioOutputAddress)),
                          static_cast<unsigned long>(usb.audioOutputRate(audioOutputAddress)));
        }
        else if (command == 'v')
        {
            EspUsbHostAudioVolumeRange range;
            if (usb.audioGetVolumeRange(range, audioAddress))
            {
                Serial.printf("AUDIO_VOLUME_RANGE min=%d max=%d res=%d\n",
                              range.min,
                              range.max,
                              range.resolution);
            }
            else
            {
                Serial.println("AUDIO_VOLUME_RANGE failed");
            }
        }
        else if (command == 'w')
        {
            const bool set = usb.audioSetVolumeDb(-6.0f, audioAddress);
            float readback = 0.0f;
            const bool got = usb.audioGetVolumeDb(readback, audioAddress);
            Serial.printf("AUDIO_VOLUME set=%u get=%u db=%.2f\n",
                          set ? 1 : 0,
                          got ? 1 : 0,
                          readback);
        }
        else if (command == 'M')
        {
            // Mute and unmute again, so the streaming steps that follow run on an
            // unmuted device and both transitions are covered.
            const bool set = usb.audioSetMute(true, audioAddress);
            bool muted = false;
            const bool got = usb.audioGetMute(muted, audioAddress);
            const bool cleared = usb.audioSetMute(false, audioAddress);
            bool mutedAfter = true;
            const bool gotAfter = usb.audioGetMute(mutedAfter, audioAddress);
            Serial.printf("AUDIO_MUTE set=%u get=%u muted=%u clear=%u get2=%u muted2=%u\n",
                          set ? 1 : 0,
                          got ? 1 : 0,
                          muted ? 1 : 0,
                          cleared ? 1 : 0,
                          gotAfter ? 1 : 0,
                          mutedAfter ? 1 : 0);
        }
    }
    delay(1);
}
