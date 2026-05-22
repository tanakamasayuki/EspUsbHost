#include <Arduino.h>
#include <EspUsbHost.h>
#include <math.h>

EspUsbHost usb;

static constexpr uint8_t MAX_PEAK_CHANNELS = 8;

static EspUsbHostAudioStreamInfo selectedStream;
static uint32_t selectedSampleRate = 0;
static bool audioInputReady = false;
static uint32_t audioBytes = 0;
static uint32_t lastPrintMs = 0;
static uint16_t peakAbs[MAX_PEAK_CHANNELS] = {0};

static bool isSupportedInputStream(uint32_t sampleRate, uint8_t, uint8_t)
{
  // en: Change this function to choose which USB Audio IN formats this sketch accepts.
  // ja: 受け入れるUSB Audio INフォーマットを変える場合は、この関数を変更します。
  return sampleRate == 48000 || sampleRate == 44100;
}

static bool selectAudioInput(uint8_t address)
{
  EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
  const size_t count = usb.getAudioStreams(address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);

  // en: Print every parsed USB Audio stream, then select the best supported input stream.
  // ja: 解析済みのUSB Audioストリームをすべて表示し、対応する最適な入力ストリームを選びます。
  for (size_t i = 0; i < count; i++)
  {
    espUsbHostPrint(streams[i]);
  }

  const EspUsbHostAudioStreamSelection selected = espUsbHostSelectAudioInputStream(streams, count, isSupportedInputStream);
  if (!selected)
  {
    return false;
  }

  selectedStream = streams[selected.index];
  selectedSampleRate = selected.sampleRate;
  audioInputReady = usb.audioInputStart(selectedStream, selected.sampleRate, address);
  if (!audioInputReady)
  {
    return false;
  }

  Serial.printf("audio input selected: addr=%u iface=%u alt=%u channels=%u bytes=%u bits=%u rate=%lu\n",
                selectedStream.address,
                selectedStream.interfaceNumber,
                selectedStream.alternate,
                selectedStream.channels,
                selectedStream.bytesPerSample,
                selectedStream.bitsPerSample,
                static_cast<unsigned long>(selected.sampleRate));
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost Audio Input example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(info);
                          if (usb.audioInputReady(info.address))
                          {
                            audioInputReady = false;
                            Serial.printf("audio input %s: addr=%u\n", selectAudioInput(info.address) ? "ready" : "unsupported", info.address);
                          } });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == selectedStream.address)
                              {
                               audioInputReady = false;
                               selectedStream = EspUsbHostAudioStreamInfo();
                               selectedSampleRate = 0;
                             } });

  usb.onAudioData([](const EspUsbHostAudioData &audio)
                  {
                    if (!audioInputReady ||
                        audio.address != selectedStream.address ||
                        audio.interfaceNumber != selectedStream.interfaceNumber)
                    {
                      return;
                    }
                    // en: Audio data arrives on the USB task, so only count bytes and print once per second.
                    // ja: 音声データはUSBタスクで届くため、バイト数を数えて1秒ごとに表示するだけにします。
                    audioBytes += audio.length;

                    // en: Track per-channel peak amplitude (16-bit PCM only) so volume changes are visible on the serial log.
                    // ja: チャンネルごとのピーク振幅(16bit PCMのみ)を保持し、音量変化をシリアルで確認できるようにします。
                    if (selectedStream.bytesPerSample == 2 && selectedStream.channels > 0 && selectedStream.channels <= MAX_PEAK_CHANNELS)
                    {
                      const uint8_t channels = selectedStream.channels;
                      const size_t frameBytes = static_cast<size_t>(channels) * 2;
                      const size_t frames = audio.length / frameBytes;
                      const uint8_t *bytes = audio.data;
                      for (size_t f = 0; f < frames; f++)
                      {
                        for (uint8_t ch = 0; ch < channels; ch++)
                        {
                          const size_t off = (f * channels + ch) * 2;
                          const int16_t s = static_cast<int16_t>(bytes[off] | (bytes[off + 1] << 8));
                          const uint16_t a = (s == INT16_MIN) ? 32768 : static_cast<uint16_t>(s < 0 ? -s : s);
                          if (a > peakAbs[ch])
                          {
                            peakAbs[ch] = a;
                          }
                        }
                      }
                    }

                    const uint32_t now = millis();
                    if (now - lastPrintMs >= 1000)
                    {
                      Serial.printf("audio: addr=%u iface=%u channels=%u bits=%u rate=%lu bytes_per_sec=%5lu",
                                    audio.address,
                                    audio.interfaceNumber,
                                    selectedStream.channels,
                                    selectedStream.bitsPerSample,
                                    static_cast<unsigned long>(selectedSampleRate),
                                    static_cast<unsigned long>(audioBytes));
                      const uint8_t channels = (selectedStream.channels <= MAX_PEAK_CHANNELS) ? selectedStream.channels : MAX_PEAK_CHANNELS;
                      for (uint8_t ch = 0; ch < channels; ch++)
                      {
                        float dbfs;
                        if (peakAbs[ch] == 0)
                        {
                          dbfs = -INFINITY;
                        }
                        else
                        {
                          dbfs = 20.0f * log10f(static_cast<float>(peakAbs[ch]) / 32768.0f);
                        }
                        Serial.printf(" ch%u_peak=%5u(%5.1fdBFS)", ch, peakAbs[ch], dbfs);
                        peakAbs[ch] = 0;
                      }
                      Serial.println();
                      audioBytes = 0;
                      lastPrintMs = now;
                    } });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(10);
}
