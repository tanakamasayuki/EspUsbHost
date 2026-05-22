#include <Arduino.h>
#include <EspUsbHost.h>
#include <PCMFlow.h>
#include "assets_embed.h"

EspUsbHost usb;
PCMFlow audio;

static constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;
static constexpr size_t BUFFER_FRAMES = 2048;
static constexpr uint8_t USB_OUTPUT_VOLUME_STEPS[] = {100, 50, 10, 0};

static bool isSupportedOutputStream(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample)
{
  // en: Change this function to choose which USB Audio OUT formats this sketch accepts.
  // ja: 受け入れるUSB Audio OUTフォーマットを変える場合は、この関数を変更します。
  return (sampleRate == 48000 || sampleRate == 44100) &&
         (channels == 1 || channels == 2) &&
         (bitsPerSample == 8 || bitsPerSample == 16);
}

static uint8_t audioAddress = 0;
static size_t fileIndex = 0;
static size_t volumeStepIndex = 0;
static bool playbackFinished = false;
static bool hardwareVolumeChecked = false;
static bool hardwareVolumeSupported = false;
static PCMFormat outputFormat = {DEFAULT_SAMPLE_RATE, 2, 16};

static float currentOutputGain()
{
  return hardwareVolumeSupported ? 1.0f : static_cast<float>(USB_OUTPUT_VOLUME_STEPS[volumeStepIndex]) / 100.0f;
}

static void configureAudioOutputVolume(uint8_t address)
{
  const uint8_t percent = USB_OUTPUT_VOLUME_STEPS[volumeStepIndex];
  if (hardwareVolumeChecked && !hardwareVolumeSupported)
  {
    audio.setGain(currentOutputGain());
    Serial.printf("PCMFlow gain: %.2f\n", currentOutputGain());
    return;
  }

  if (usb.audioConfigureVolumePercent(percent, address))
  {
    hardwareVolumeChecked = true;
    hardwareVolumeSupported = true;
    audio.setGain(1.0f);
    Serial.printf("audio hardware volume: %u%%\n", percent);
  }
  else
  {
    hardwareVolumeChecked = true;
    hardwareVolumeSupported = false;
    audio.setGain(currentOutputGain());
    Serial.println("audio hardware volume unsupported");
    Serial.printf("PCMFlow gain: %.2f\n", currentOutputGain());
  }
}

static bool nextVolumeLoop()
{
  if (volumeStepIndex + 1 >= sizeof(USB_OUTPUT_VOLUME_STEPS) / sizeof(USB_OUTPUT_VOLUME_STEPS[0]))
  {
    Serial.println("all volume loops played");
    playbackFinished = true;
    return false;
  }

  volumeStepIndex++;
  fileIndex = 0;
  if (audioAddress)
  {
    configureAudioOutputVolume(audioAddress);
  }
  Serial.printf("restart playlist at %u%% volume\n", USB_OUTPUT_VOLUME_STEPS[volumeStepIndex]);
  return true;
}

static bool openNextFile()
{
  audio.close();

  // en: Walk through embedded assets and open the next MP3 file for decoding.
  // ja: 埋め込みアセットを順に調べ、次のMP3ファイルをデコード用に開きます。
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
    audio.setGain(currentOutputGain());
    if (audio.open(assets_file_data[index], assets_file_sizes[index], PCMFlow::CodecKind::Mp3))
    {
      Serial.printf("playing: %s (%u bytes)\n", name, static_cast<unsigned>(assets_file_sizes[index]));
      return true;
    }

    Serial.printf("PCMFlow open failed: %s error=%u\n",
                  name,
                  static_cast<unsigned>(audio.lastError()));
  }

  Serial.printf("playlist finished at %u%% volume\n", USB_OUTPUT_VOLUME_STEPS[volumeStepIndex]);
  if (nextVolumeLoop())
  {
    return openNextFile();
  }
  return false;
}

static void restartCurrentFile()
{
  // en: Reopen the current file after the USB device format has been selected.
  // ja: USBデバイス側の形式が決まったあと、現在のファイルをその形式で開き直します。
  if (fileIndex > 0)
  {
    fileIndex--;
  }
  openNextFile();
}

static bool chooseAudioOutputStream(uint8_t address)
{
  EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
  const size_t count = usb.getAudioStreams(address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
  // en: Pick the first USB Audio OUT stream that PCMFlow can feed directly.
  // ja: PCMFlowが直接供給できる最初のUSB Audio OUTストリームを選びます。
  for (size_t i = 0; i < count; i++)
  {
    espUsbHostPrint(streams[i]);
  }

  const EspUsbHostAudioStreamSelection selected = espUsbHostSelectAudioOutputStream(streams, count, isSupportedOutputStream);
  if (!selected)
  {
    return false;
  }

  const EspUsbHostAudioStreamInfo &stream = streams[selected.index];
  outputFormat = {selected.sampleRate, stream.channels, stream.bitsPerSample};
  Serial.printf("selected PCMFlow output: %lu Hz, %u ch, %u-bit\n",
                static_cast<unsigned long>(outputFormat.sampleRate),
                outputFormat.channels,
                outputFormat.bitsPerSample);
  // en: PCMFlow output format must match the selected USB stream before playback starts.
  // ja: 再生開始前に、PCMFlowの出力形式を選択したUSBストリームに合わせます。
  restartCurrentFile();
  return usb.audioOutputStart(stream, selected.sampleRate, address);
}

static void fillAudioOutput(EspUsbHostAudioOutputRequest &request)
{
  // en: This callback runs from the USB task; only copy already-decoded PCM frames.
  // ja: このコールバックはUSBタスクから呼ばれるため、デコード済みPCMフレームのコピーだけを行います。
  request.writtenFrames = audio.readFrames(request.data, request.frameCount);
}

void setup()
{
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost Audio Output MP3 PCMFlow example start");

  if (!openNextFile())
  {
    Serial.println("no MP3 files found in assets");
  }

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(info);
                          // en: Wait until the library has parsed usable USB Audio OUT interfaces.
                          // ja: ライブラリが利用可能なUSB Audio OUTインターフェースを解析するまで待ちます。
                          if (!usb.audioOutputReady(info.address))
                          {
                            return;
                          }

                          if (chooseAudioOutputStream(info.address))
                          {
                            audioAddress = info.address;
                            configureAudioOutputVolume(info.address);
                          }
                          Serial.printf("audio output %s: addr=%u\n", audioAddress == info.address ? "ready" : "unsupported", info.address); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == audioAddress)
                             {
                               audioAddress = 0;
                               hardwareVolumeChecked = false;
                               hardwareVolumeSupported = false;
                             } });

  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           { fillAudioOutput(request); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (playbackFinished)
  {
    delay(10);
    return;
  }

  // en: Keep PCMFlow decoding outside the USB callback and advance to the next file at EOF.
  // ja: PCMFlowのデコードはUSBコールバック外で進め、終端に達したら次のファイルへ進みます。
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
