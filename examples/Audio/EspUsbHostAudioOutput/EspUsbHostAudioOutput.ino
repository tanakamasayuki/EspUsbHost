#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr uint32_t TONE_HZ = 440;
static constexpr uint8_t CHANNELS = 1;
static constexpr uint8_t BYTES_PER_SAMPLE = 2;
static constexpr uint8_t BITS_PER_SAMPLE = 16;
static constexpr size_t FRAMES_PER_PACKET = SAMPLE_RATE / 1000;
static constexpr size_t MAX_CHANNELS = 2;
static uint8_t audioAddress = 0;
static bool audioReady = false;
static uint8_t audioChannels = CHANNELS;
static size_t audioPacketBytes = FRAMES_PER_PACKET * CHANNELS * BYTES_PER_SAMPLE;
static uint8_t samples[FRAMES_PER_PACKET * MAX_CHANNELS * BYTES_PER_SAMPLE];

static void fillTone()
{
  static uint32_t phase = 0;
  for (size_t frame = 0; frame < FRAMES_PER_PACKET; frame++)
  {
    const int16_t value = (phase < SAMPLE_RATE / 2) ? 12000 : -12000;
    phase += TONE_HZ;
    if (phase >= SAMPLE_RATE)
    {
      phase -= SAMPLE_RATE;
    }
    for (uint8_t channel = 0; channel < audioChannels; channel++)
    {
      const size_t offset = (frame * audioChannels + channel) * BYTES_PER_SAMPLE;
      samples[offset] = value & 0xff;
      samples[offset + 1] = (value >> 8) & 0xff;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHost Audio Output example start");
  usb.setAudioSampleRate(SAMPLE_RATE);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.printf("connected: addr=%u portId=0x%02x vid=%04x pid=%04x product=%s\n",
                                        info.address,
                                        info.portId,
                                        info.vid,
                                        info.pid,
                                        info.product);
                          if (usb.audioOutputReady(info.address))
                          {
                            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                            const size_t count = usb.getAudioStreams(info.address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                            for (size_t i = 0; i < count; i++)
                            {
                              Serial.printf("audio stream: iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu rates=%u max=%u interval=%u\n",
                                            streams[i].interfaceNumber,
                                            streams[i].alternate,
                                            streams[i].endpointAddress,
                                            streams[i].input ? "IN" : "OUT",
                                            streams[i].channels,
                                            streams[i].bytesPerSample,
                                            streams[i].bitsPerSample,
                                            static_cast<unsigned long>(streams[i].sampleRate),
                                            streams[i].sampleRateCount,
                                            streams[i].maxPacketSize,
                                            streams[i].interval);
                              if (streams[i].output &&
                                  streams[i].channels >= CHANNELS &&
                                  streams[i].channels <= MAX_CHANNELS &&
                                  espUsbHostAudioStreamMatchesPcm(streams[i],
                                                                  streams[i].channels,
                                                                  BYTES_PER_SAMPLE,
                                                                  BITS_PER_SAMPLE,
                                                                  SAMPLE_RATE))
                              {
                                audioAddress = info.address;
                                audioReady = true;
                                audioChannels = streams[i].channels;
                                audioPacketBytes = FRAMES_PER_PACKET * audioChannels * BYTES_PER_SAMPLE;
                                usb.setAudioSampleRate(SAMPLE_RATE, info.address);
                              }
                            }
                            Serial.printf("audio output %s: addr=%u\n", audioReady ? "ready" : "unsupported", info.address);
                          } });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.printf("disconnected: addr=%u vid=%04x pid=%04x\n",
                                           info.address,
                                           info.vid,
                                           info.pid);
                             if (info.address == audioAddress)
                             {
                               audioReady = false;
                               audioAddress = 0;
                             } });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (audioReady)
  {
    fillTone();
    usb.audioSend(samples, audioPacketBytes, audioAddress);
    delay(1);
  }
  else
  {
    delay(10);
  }
}
