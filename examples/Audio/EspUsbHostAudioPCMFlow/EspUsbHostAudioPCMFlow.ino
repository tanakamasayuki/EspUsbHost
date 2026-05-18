#include <Arduino.h>
#include <EspUsbHost.h>
#include <PCMFlow.h>

EspUsbHost usb;
PCMFlow audio;

static constexpr uint32_t SOURCE_SAMPLE_RATE = 22050;
static constexpr uint16_t SOURCE_CHANNELS = 1;
static constexpr uint16_t SOURCE_BITS_PER_SAMPLE = 16;
static constexpr uint32_t SOURCE_SECONDS = 4;
static constexpr uint32_t SOURCE_FRAMES = SOURCE_SAMPLE_RATE * SOURCE_SECONDS;
static constexpr uint32_t SOURCE_DATA_BYTES = SOURCE_FRAMES * SOURCE_CHANNELS * (SOURCE_BITS_PER_SAMPLE / 8);
static constexpr uint32_t SOURCE_RIFF_BYTES = 36 + SOURCE_DATA_BYTES;

static constexpr uint32_t OUTPUT_SAMPLE_RATE = 48000;
static constexpr uint8_t OUTPUT_CHANNELS = 2;
static constexpr uint8_t OUTPUT_BITS_PER_SAMPLE = 16;
static constexpr uint8_t OUTPUT_BYTES_PER_SAMPLE = OUTPUT_BITS_PER_SAMPLE / 8;

static uint8_t audioAddress = 0;

class GeneratedToneWavStream : public ByteStream
{
public:
  void rewind()
  {
    pos_ = 0;
  }

  size_t read(void *dst, size_t count) override
  {
    if (!dst || count == 0)
    {
      return 0;
    }

    uint8_t *out = static_cast<uint8_t *>(dst);
    size_t written = 0;
    while (written < count && pos_ < size())
    {
      out[written++] = byteAt(pos_++);
    }
    return written;
  }

  bool isEof() const override
  {
    return pos_ >= size();
  }

  bool isSeekable() const override
  {
    return true;
  }

  bool seek(size_t offset) override
  {
    if (offset > size())
    {
      return false;
    }
    pos_ = offset;
    return true;
  }

  size_t size() const override
  {
    return 44 + SOURCE_DATA_BYTES;
  }

  size_t position() const override
  {
    return pos_;
  }

private:
  size_t pos_ = 0;

  static uint8_t le16(uint16_t value, uint8_t index)
  {
    return (value >> (index * 8)) & 0xff;
  }

  static uint8_t le32(uint32_t value, uint8_t index)
  {
    return (value >> (index * 8)) & 0xff;
  }

  static uint8_t headerByte(size_t offset)
  {
    static constexpr uint32_t byteRate = SOURCE_SAMPLE_RATE * SOURCE_CHANNELS * (SOURCE_BITS_PER_SAMPLE / 8);
    static constexpr uint16_t blockAlign = SOURCE_CHANNELS * (SOURCE_BITS_PER_SAMPLE / 8);

    switch (offset)
    {
    case 0: return 'R';
    case 1: return 'I';
    case 2: return 'F';
    case 3: return 'F';
    case 4: return le32(SOURCE_RIFF_BYTES, 0);
    case 5: return le32(SOURCE_RIFF_BYTES, 1);
    case 6: return le32(SOURCE_RIFF_BYTES, 2);
    case 7: return le32(SOURCE_RIFF_BYTES, 3);
    case 8: return 'W';
    case 9: return 'A';
    case 10: return 'V';
    case 11: return 'E';
    case 12: return 'f';
    case 13: return 'm';
    case 14: return 't';
    case 15: return ' ';
    case 16: return 16;
    case 20: return 1;
    case 22: return le16(SOURCE_CHANNELS, 0);
    case 23: return le16(SOURCE_CHANNELS, 1);
    case 24: return le32(SOURCE_SAMPLE_RATE, 0);
    case 25: return le32(SOURCE_SAMPLE_RATE, 1);
    case 26: return le32(SOURCE_SAMPLE_RATE, 2);
    case 27: return le32(SOURCE_SAMPLE_RATE, 3);
    case 28: return le32(byteRate, 0);
    case 29: return le32(byteRate, 1);
    case 30: return le32(byteRate, 2);
    case 31: return le32(byteRate, 3);
    case 32: return le16(blockAlign, 0);
    case 33: return le16(blockAlign, 1);
    case 34: return le16(SOURCE_BITS_PER_SAMPLE, 0);
    case 35: return le16(SOURCE_BITS_PER_SAMPLE, 1);
    case 36: return 'd';
    case 37: return 'a';
    case 38: return 't';
    case 39: return 'a';
    case 40: return le32(SOURCE_DATA_BYTES, 0);
    case 41: return le32(SOURCE_DATA_BYTES, 1);
    case 42: return le32(SOURCE_DATA_BYTES, 2);
    case 43: return le32(SOURCE_DATA_BYTES, 3);
    default: return 0;
    }
  }

  static uint8_t sampleByte(size_t offset)
  {
    const size_t sampleIndex = offset / 2;
    const size_t byteIndex = offset % 2;
    const int16_t value = ((sampleIndex / 25) & 1) ? 8000 : -8000;
    return byteIndex == 0 ? (value & 0xff) : ((value >> 8) & 0xff);
  }

  static uint8_t byteAt(size_t offset)
  {
    if (offset < 44)
    {
      return headerByte(offset);
    }
    return sampleByte(offset - 44);
  }
};

GeneratedToneWavStream source;

static void restartPcmFlow()
{
  audio.end();
  source.rewind();
  audio.setInput(&source, PCMFlow::CodecKind::Wav);
  audio.setOutputFormat({OUTPUT_SAMPLE_RATE, OUTPUT_CHANNELS, OUTPUT_BITS_PER_SAMPLE});
  audio.setBufferFrames(2048);
  audio.setGain(0.8f);
  if (!audio.begin())
  {
    Serial.printf("PCMFlow begin failed: %u\n", static_cast<unsigned>(audio.lastError()));
  }
}

static void pumpPcmFlow()
{
  for (uint8_t i = 0; i < 4 && !audio.isEof(); i++)
  {
    audio.pump();
  }
  if (audio.isEof())
  {
    restartPcmFlow();
  }
}

static void fillAudioOutput(EspUsbHostAudioOutputRequest &request)
{
  pumpPcmFlow();
  request.writtenFrames = audio.readFrames(request.data, request.frameCount);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHost Audio PCMFlow example start");

  restartPcmFlow();
  usb.setAudioSampleRate(OUTPUT_SAMPLE_RATE);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.printf("connected: addr=%u portId=0x%02x vid=%04x pid=%04x product=%s\n",
                                        info.address,
                                        info.portId,
                                        info.vid,
                                        info.pid,
                                        info.product);
                          if (!usb.audioOutputReady(info.address))
                          {
                            return;
                          }

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
                                espUsbHostAudioStreamMatchesPcm(streams[i],
                                                                OUTPUT_CHANNELS,
                                                                OUTPUT_BYTES_PER_SAMPLE,
                                                                OUTPUT_BITS_PER_SAMPLE,
                                                                OUTPUT_SAMPLE_RATE))
                            {
                              audioAddress = info.address;
                              usb.setAudioSampleRate(OUTPUT_SAMPLE_RATE, info.address);
                              usb.audioOutputStart(info.address);
                            }
                          }
                          Serial.printf("audio output %s: addr=%u\n", audioAddress == info.address ? "ready" : "unsupported", info.address);
                        });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.printf("disconnected: addr=%u vid=%04x pid=%04x\n",
                                           info.address,
                                           info.vid,
                                           info.pid);
                             if (info.address == audioAddress)
                             {
                               audioAddress = 0;
                             }
                           });

  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           {
                             fillAudioOutput(request);
                           });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(10);
}
