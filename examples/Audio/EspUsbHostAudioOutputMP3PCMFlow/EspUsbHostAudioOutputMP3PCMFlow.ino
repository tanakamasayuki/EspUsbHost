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
  // en: Reopen the current file after the USB device format has been selected.
  // ja: USBデバイス側の形式が決まったあと、現在のファイルをその形式で開き直します。
  if (fileIndex > 0)
  {
    fileIndex--;
  }
  openNextFile();
}

static bool isSupportedOutputStream(const EspUsbHostAudioStreamInfo &stream)
{
  // en: PCMFlow can produce mono/stereo 8-bit or 16-bit PCM for this example.
  // ja: このサンプルではPCMFlowからモノラル/ステレオの8bitまたは16bit PCMを出力します。
  return stream.output &&
         (stream.channels == 1 || stream.channels == 2) &&
         (stream.bitsPerSample == 8 || stream.bitsPerSample == 16);
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

    if (!isSupportedOutputStream(streams[i]))
    {
      continue;
    }

    const uint32_t sampleRate = espUsbHostAudioStreamPreferredSampleRate(streams[i], DEFAULT_SAMPLE_RATE);
    if (sampleRate == 0 || !espUsbHostAudioStreamSupportsSampleRate(streams[i], sampleRate))
    {
      continue;
    }

    outputFormat = {sampleRate, streams[i].channels, streams[i].bitsPerSample};
    Serial.printf("selected PCMFlow output: %lu Hz, %u ch, %u-bit\n",
                  static_cast<unsigned long>(outputFormat.sampleRate),
                  outputFormat.channels,
                  outputFormat.bitsPerSample);
    // en: PCMFlow output format must match the selected USB stream before playback starts.
    // ja: 再生開始前に、PCMFlowの出力形式を選択したUSBストリームに合わせます。
    restartCurrentFile();
    return usb.audioOutputStart(streams[i], outputFormat.sampleRate, address);
  }
  return false;
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
  delay(500);
  Serial.println("EspUsbHost Audio Output MP3 PCMFlow example start");

  if (!openNextFile())
  {
    Serial.println("no MP3 files found in assets");
  }

  usb.setAudioSampleRate(DEFAULT_SAMPLE_RATE);

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
                          }
                          Serial.printf("audio output %s: addr=%u\n", audioAddress == info.address ? "ready" : "unsupported", info.address); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == audioAddress)
                             {
                               audioAddress = 0;
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
