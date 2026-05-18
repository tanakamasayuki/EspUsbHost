#include <Arduino.h>
#include <EspUsbHost.h>
#include <PCMFlow.h>
#include "assets_embed.h"

EspUsbHost usb;
PCMFlow audio;

static constexpr uint32_t OUTPUT_SAMPLE_RATE = 48000;
static constexpr uint8_t OUTPUT_CHANNELS = 2;
static constexpr uint8_t OUTPUT_BITS_PER_SAMPLE = 16;
static constexpr uint8_t OUTPUT_BYTES_PER_SAMPLE = OUTPUT_BITS_PER_SAMPLE / 8;
static constexpr size_t BUFFER_FRAMES = 2048;

static uint8_t audioAddress = 0;
static size_t fileIndex = 0;

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

    audio.setOutputFormat({OUTPUT_SAMPLE_RATE, OUTPUT_CHANNELS, OUTPUT_BITS_PER_SAMPLE});
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
