#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static constexpr uint32_t PREFERRED_SAMPLE_RATE = 48000; // 0: use the first input stream rate
static constexpr uint8_t PREFERRED_CHANNELS = 0;         // 0: use the first matching input stream
static constexpr uint8_t PREFERRED_BITS_PER_SAMPLE = 0;  // 0: use the first matching input stream

static EspUsbHostAudioStreamInfo selectedStream;
static bool audioInputReady = false;
static uint32_t audioBytes = 0;
static uint32_t lastPrintMs = 0;

static bool matchesPreferredInput(const EspUsbHostAudioStreamInfo &stream)
{
  if (!stream.input)
  {
    return false;
  }
  if (PREFERRED_SAMPLE_RATE != 0 && !espUsbHostAudioStreamSupportsSampleRate(stream, PREFERRED_SAMPLE_RATE))
  {
    return false;
  }
  if (PREFERRED_CHANNELS != 0 && stream.channels != PREFERRED_CHANNELS)
  {
    return false;
  }
  if (PREFERRED_BITS_PER_SAMPLE != 0 && stream.bitsPerSample != PREFERRED_BITS_PER_SAMPLE)
  {
    return false;
  }
  return true;
}

static bool selectAudioInput(uint8_t address)
{
  EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
  const size_t count = usb.getAudioStreams(address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);

  // en: Print every parsed USB Audio stream, then choose the first stream matching the constants above.
  // ja: 解析済みのUSB Audioストリームをすべて表示し、上の定数に合う最初のストリームを選びます。
  for (size_t i = 0; i < count; i++)
  {
    espUsbHostPrint(streams[i]);
    if (!audioInputReady && matchesPreferredInput(streams[i]))
    {
      selectedStream = streams[i];
      selectedStream.sampleRate = espUsbHostAudioStreamPreferredSampleRate(streams[i], PREFERRED_SAMPLE_RATE);
      if (selectedStream.sampleRate == 0)
      {
        continue;
      }
      audioInputReady = usb.audioInputStart(selectedStream, selectedStream.sampleRate, address);
    }
  }

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
                static_cast<unsigned long>(selectedStream.sampleRate));
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(500);
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
                    const uint32_t now = millis();
                    if (now - lastPrintMs >= 1000)
                    {
                      Serial.printf("audio: addr=%u iface=%u channels=%u bits=%u rate=%lu bytes_per_sec=%lu last_chunk=%u\n",
                                    audio.address,
                                    audio.interfaceNumber,
                                    selectedStream.channels,
                                    selectedStream.bitsPerSample,
                                    static_cast<unsigned long>(selectedStream.sampleRate),
                                    static_cast<unsigned long>(audioBytes),
                                    static_cast<unsigned>(audio.length));
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
