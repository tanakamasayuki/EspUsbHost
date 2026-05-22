#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static EspUsbHostAudioStreamInfo selectedStream;
static uint32_t selectedSampleRate = 0;
static bool audioInputReady = false;
static uint32_t audioBytes = 0;
static uint32_t lastPrintMs = 0;

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
                    const uint32_t now = millis();
                    if (now - lastPrintMs >= 1000)
                    {
                      Serial.printf("audio: addr=%u iface=%u channels=%u bits=%u rate=%lu bytes_per_sec=%lu last_chunk=%u\n",
                                    audio.address,
                                    audio.interfaceNumber,
                                    selectedStream.channels,
                                    selectedStream.bitsPerSample,
                                    static_cast<unsigned long>(selectedSampleRate),
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
