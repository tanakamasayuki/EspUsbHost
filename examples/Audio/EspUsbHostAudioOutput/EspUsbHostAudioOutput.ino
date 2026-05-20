#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr uint32_t TONE_HZ = 440;
static constexpr uint8_t CHANNELS = 1;
static constexpr uint8_t BYTES_PER_SAMPLE = 2;
static constexpr uint8_t BITS_PER_SAMPLE = 16;
static constexpr int16_t VOLUME = 100; // 0-32767
static uint8_t audioAddress = 0;

static void fillTone(EspUsbHostAudioOutputRequest &request)
{
  static uint32_t phase = 0;
  for (size_t frame = 0; frame < request.frameCount; frame++)
  {
    const int16_t value = (phase < SAMPLE_RATE / 2) ? VOLUME : -VOLUME;
    phase += TONE_HZ;
    if (phase >= SAMPLE_RATE)
    {
      phase -= SAMPLE_RATE;
    }
    for (uint8_t channel = 0; channel < request.channels; channel++)
    {
      const size_t offset = (frame * request.channels + channel) * BYTES_PER_SAMPLE;
      request.data[offset] = value & 0xff;
      request.data[offset + 1] = (value >> 8) & 0xff;
    }
  }
  request.writtenFrames = request.frameCount;
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHost Audio Output example start");
  usb.setAudioSampleRate(SAMPLE_RATE);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(info);
                          if (usb.audioOutputReady(info.address))
                          {
                            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                            const size_t count = usb.getAudioStreams(info.address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                            for (size_t i = 0; i < count; i++)
                            {
                              espUsbHostPrint(streams[i]);
                              if (streams[i].output &&
                                  espUsbHostAudioStreamMatchesPcm(streams[i],
                                                                  CHANNELS,
                                                                  BYTES_PER_SAMPLE,
                                                                  BITS_PER_SAMPLE,
                                                                  SAMPLE_RATE))
                              {
                                audioAddress = info.address;
                                usb.setAudioSampleRate(SAMPLE_RATE, info.address);
                                usb.audioOutputStart(info.address);
                              }
                            }
                            Serial.printf("audio output %s: addr=%u\n", audioAddress == info.address ? "ready" : "unsupported", info.address);
                          } });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == audioAddress)
                             {
                               audioAddress = 0;
                             } });

  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           { fillTone(request); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(10);
}
