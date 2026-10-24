// en: Connect PCMFlow decoding to a selected USB audio stream.
// ja: PCMFlowのデコード結果を選択したUSB音声streamへ接続する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <Arduino.h>
#include <EspUsbHost.h>
#include <PCMFlow.h>
#include "assets_embed.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;
PCMFlow audio;

static constexpr float OUTPUT_GAIN = 0.8f;

static uint8_t audioAddress = 0;
static size_t fileIndex = 0;
static bool playbackFinished = false;
static PCMFormat outputFormat = {48000, 2, 16};

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
    audio.setGain(OUTPUT_GAIN);
    if (audio.open(assets_file_data[index], assets_file_sizes[index], PCMFlow::CodecKind::Mp3))
    {
      playbackFinished = false;
      Serial.printf("playing: %s (%u bytes)\n", name, static_cast<unsigned>(assets_file_sizes[index]));
      return true;
    }

    Serial.printf("PCMFlow open failed: %s error=%u\n",
                  name,
                  static_cast<unsigned>(audio.lastError()));
  }

  Serial.println("all files played");
  playbackFinished = true;
  return false;
}

static bool chooseAudioOutputStream(uint8_t address)
{
  EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
  const size_t count = usb.getAudioStreams(address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
  // en: Print each candidate, then let the library score and select the best stream. In most cases this prefers 48 kHz, 16-bit, stereo when available.
  // ja: 各候補を表示し、ライブラリ側のスコアリングで最適なストリームを選びます。 多くの場合、利用可能なら48 kHz、16-bit、ステレオが優先されます。
  for (size_t i = 0; i < count; i++)
  {
    espUsbHostPrint(streams[i]);
  }

  const EspUsbHostAudioStreamSelection selected = espUsbHostSelectAudioOutputStream(streams, count);
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
  return usb.audioOutputStart(stream, selected.sampleRate, address);
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost Audio Output MP3 PCMFlow example start");

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
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
                            fileIndex = 0;
                            if (!openNextFile())
                            {
                              Serial.println("no MP3 files found in assets");
                            }
                          }
                          Serial.printf("audio output %s: addr=%u\n", audioAddress == info.address ? "ready" : "unsupported", info.address); });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == audioAddress)
                             {
                               audioAddress = 0;
                               audio.close();
                             } });

  // en: Fill only the requested PCM frames and return promptly; do not decode a file in this callback.
  // ja: 要求されたPCM frameを用意して短く戻る。このcallbackでファイルのデコードを行わない。
  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           {
                             // en: This callback runs from the USB task; only copy already-decoded PCM frames.
                             // ja: このコールバックはUSBタスクから呼ばれるため、デコード済みPCMフレームのコピーだけを行います。
                             request.writtenFrames = audio.readFrames(request.data, request.frameCount);
                           });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (!audioAddress)
  {
    delay(10);
    return;
  }

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
