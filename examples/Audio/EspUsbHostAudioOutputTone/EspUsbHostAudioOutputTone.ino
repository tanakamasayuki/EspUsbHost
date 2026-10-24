// en: Generate PCM tones for a compatible USB audio output.
// ja: 対応するUSB音声出力へPCMの試験音を生成する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <Arduino.h>
#include <EspUsbHost.h>

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static constexpr uint32_t TONE_HZ_LEFT = 440;
static constexpr uint32_t TONE_HZ_RIGHT = 880;
static constexpr int16_t VOLUME = 100; // 0-32767

static bool isSupportedOutputStream(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample)
{
  // en: Change this function to choose which USB Audio OUT formats this sketch accepts.
  // ja: 受け入れるUSB Audio OUTフォーマットを変える場合は、この関数を変更します。
  return sampleRate == 48000 &&
         channels == 2 &&
         bitsPerSample == 16;
}

static uint8_t audioAddress = 0;

static void fillTone(EspUsbHostAudioOutputRequest &request)
{
  static uint32_t phaseLeft = 0;
  static uint32_t phaseRight = 0;
  // en: Generate only the frames requested by the USB Audio scheduler.
  // ja: USB Audioのスケジューラが要求したフレーム数だけ生成します。
  for (size_t frame = 0; frame < request.frameCount; frame++)
  {
    const int16_t valueLeft = (phaseLeft < request.sampleRate / 2) ? VOLUME : -VOLUME;
    const int16_t valueRight = (phaseRight < request.sampleRate / 2) ? VOLUME : -VOLUME;
    phaseLeft += TONE_HZ_LEFT;
    phaseRight += TONE_HZ_RIGHT;
    if (phaseLeft >= request.sampleRate)
    {
      phaseLeft -= request.sampleRate;
    }
    if (phaseRight >= request.sampleRate)
    {
      phaseRight -= request.sampleRate;
    }
    for (uint8_t channel = 0; channel < request.channels; channel++)
    {
      const int16_t value = (channel == 0) ? valueLeft : valueRight;
      const size_t offset = (frame * request.channels + channel) * request.bytesPerSample;
      request.data[offset] = value & 0xff;
      request.data[offset + 1] = (value >> 8) & 0xff;
    }
  }
  request.writtenFrames = request.frameCount;
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost Audio Output Tone example start");

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(info);
                          if (usb.audioOutputReady(info.address))
                          {
                            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                            const size_t count = usb.getAudioStreams(info.address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                            // en: Start output on the first stream that exactly matches this tone format.
                            // ja: このトーン生成形式に完全一致する最初のストリームで出力を開始します。
                            for (size_t i = 0; i < count; i++)
                            {
                              espUsbHostPrint(streams[i]);
                            }
                            const EspUsbHostAudioStreamSelection selected = espUsbHostSelectAudioOutputStream(streams, count, isSupportedOutputStream);
                            if (selected)
                            {
                              if (usb.audioOutputStart(streams[selected.index], selected.sampleRate, info.address))
                              {
                                audioAddress = info.address;
                              }
                            }
                            Serial.printf("audio output %s: addr=%u\n", audioAddress == info.address ? "ready" : "unsupported", info.address);
                          } });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == audioAddress)
                             {
                               audioAddress = 0;
                             } });

  // en: Fill only the requested PCM frames and return promptly; do not decode a file in this callback.
  // ja: 要求されたPCM frameを用意して短く戻る。このcallbackでファイルのデコードを行わない。
  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           {
                             // en: Keep this callback short; it runs on the USB client task.
                             // ja: このコールバックはUSBクライアントタスク上で動くため、短時間で戻します。
                             fillTone(request); });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  static uint32_t lastReportMs = 0;
  if (audioAddress != 0 && millis() - lastReportMs >= 1000)
  {
    lastReportMs = millis();
    Serial.printf("audio out: rate=%lu feedback=%lu underruns=%lu\n",
                  static_cast<unsigned long>(usb.audioOutputRate(audioAddress)),
                  static_cast<unsigned long>(usb.audioOutputFeedbackRate(audioAddress)),
                  static_cast<unsigned long>(usb.audioOutputUnderruns(audioAddress)));
  }
  delay(10);
}
