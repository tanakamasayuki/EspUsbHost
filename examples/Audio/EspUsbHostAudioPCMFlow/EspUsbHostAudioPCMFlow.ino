#include <Arduino.h>
#include <EspUsbHost.h>
#include <PCMFlow.h>
#include "assets_embed.h"

EspUsbHost usb;
PCMFlow audio;

static constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;
static constexpr size_t BUFFER_FRAMES = 2048;

static uint8_t audioAddress = 0;
static size_t fileIndex = 0;
static PCMFormat outputFormat = {DEFAULT_SAMPLE_RATE, 2, 16};

static bool openNextFile()
{
  audio.close();

  while (fileIndex < assets_file_count)
  {
    const size_t index = fileIndex++;
    const char *name = assets_file_names[index];
    const size_t nameLen = strlen(name);
    if (nameLen < 4 || strcmp(name + nameLen - 4, ".mp3") != 0)
    {
      continue;
    }

    audio.setOutputFormat(outputFormat);
    audio.setBufferFrames(BUFFER_FRAMES);
    audio.setGain(0.8f);
    if (audio.open(assets_file_data[index], assets_file_sizes[index], PCMFlow::CodecKind::Mp3))
    {
      Serial.printf("playing: %s (%u bytes)\n", name, static_cast<unsigned>(assets_file_sizes[index]));
      return true;
    }

    Serial.printf("PCMFlow open failed: %s error=%u\n",
                  name,
                  static_cast<unsigned>(audio.lastError()));
  }

  Serial.println("all files played");
  return false;
}

static void restartCurrentFile()
{
  if (fileIndex > 0)
  {
    fileIndex--;
  }
  openNextFile();
}

static uint32_t chooseSampleRate(const EspUsbHostAudioStreamInfo &stream)
{
  if (espUsbHostAudioStreamSupportsSampleRate(stream, DEFAULT_SAMPLE_RATE))
  {
    return DEFAULT_SAMPLE_RATE;
  }
  if (stream.sampleRate > 0 && espUsbHostAudioStreamSupportsSampleRate(stream, stream.sampleRate))
  {
    return stream.sampleRate;
  }
  if (stream.sampleRateCount > 0)
  {
    return stream.sampleRates[0];
  }
  if (stream.sampleRateMin > 0)
  {
    return stream.sampleRateMin;
  }
  return 0;
}

static bool chooseAudioOutputStream(uint8_t address)
{
  EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
  const size_t count = usb.getAudioStreams(address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
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

    if (!streams[i].output ||
        (streams[i].channels != 1 && streams[i].channels != 2) ||
        (streams[i].bitsPerSample != 8 && streams[i].bitsPerSample != 16))
    {
      continue;
    }

    const uint32_t sampleRate = chooseSampleRate(streams[i]);
    if (sampleRate == 0 || !espUsbHostAudioStreamSupportsSampleRate(streams[i], sampleRate))
    {
      continue;
    }

    outputFormat = {sampleRate, streams[i].channels, streams[i].bitsPerSample};
    Serial.printf("selected PCMFlow output: %lu Hz, %u ch, %u-bit\n",
                  static_cast<unsigned long>(outputFormat.sampleRate),
                  outputFormat.channels,
                  outputFormat.bitsPerSample);
    restartCurrentFile();
    usb.setAudioSampleRate(outputFormat.sampleRate, address);
    return usb.audioOutputStart(address);
  }
  return false;
}

static void fillAudioOutput(EspUsbHostAudioOutputRequest &request)
{
  request.writtenFrames = audio.readFrames(request.data, request.frameCount);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHost Audio PCMFlow example start");

  if (!openNextFile())
  {
    Serial.println("no MP3 files found in assets");
  }

  usb.setAudioSampleRate(DEFAULT_SAMPLE_RATE);

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

                          if (chooseAudioOutputStream(info.address))
                          {
                            audioAddress = info.address;
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
  if (audio.isEof())
  {
    openNextFile();
  }
  else
  {
    audio.pump();
  }
  delay(1);
}
