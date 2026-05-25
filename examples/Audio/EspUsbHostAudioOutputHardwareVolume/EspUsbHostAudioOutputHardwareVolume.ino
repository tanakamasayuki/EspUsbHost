#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static constexpr uint32_t TONE_HZ_LEFT = 440;
static constexpr uint32_t TONE_HZ_RIGHT = 880;
static constexpr int16_t VOLUME = 100; // 0-32767
static constexpr float USB_OUTPUT_VOLUME_DB = -12.0f;
static constexpr uint32_t AUDIO_FEATURE_MUTE_CONTROL = 1UL << 0;
static constexpr uint32_t AUDIO_FEATURE_VOLUME_CONTROL = 1UL << 1;

static bool isSupportedOutputStream(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample)
{
  // en: Use a fixed tone format so this example can focus on Feature Unit volume control.
  // ja: Feature Unitのボリューム制御に集中できるよう、トーン出力形式は固定します。
  return sampleRate == 48000 &&
         channels == 2 &&
         bitsPerSample == 16;
}

static uint8_t audioAddress = 0;

static uint8_t selectAudioOutputFeatureUnit(const EspUsbHostAudioFeatureUnitInfo *units, size_t count)
{
  // en: Prefer a unit that can unmute the output; otherwise use the first volume-capable unit.
  // ja: mute解除できるユニットを優先し、なければ最初のvolume対応ユニットを使います。
  uint8_t volumeUnit = 0;
  for (size_t i = 0; i < count && i < ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS; i++)
  {
    if (units[i].masterControls & AUDIO_FEATURE_MUTE_CONTROL)
    {
      return units[i].unitId;
    }
    if (volumeUnit == 0 && (units[i].masterControls & AUDIO_FEATURE_VOLUME_CONTROL))
    {
      volumeUnit = units[i].unitId;
    }
  }
  return volumeUnit;
}

static void configureAudioOutputVolume(uint8_t address)
{
  EspUsbHostAudioFeatureUnitInfo units[ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS];
  const size_t count = usb.getAudioFeatureUnits(address, units, ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS);
  // en: Print parsed Feature Units so unsupported devices are easy to diagnose.
  // ja: 非対応デバイスを判別しやすいよう、解析済みFeature Unitを表示します。
  for (size_t i = 0; i < count && i < ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS; i++)
  {
    Serial.printf("audio feature unit: unit=%u source=%u channels=%u master=0x%lx\n",
                  units[i].unitId,
                  units[i].sourceId,
                  units[i].channelCount,
                  static_cast<unsigned long>(units[i].masterControls));
  }

  const uint8_t unitId = selectAudioOutputFeatureUnit(units, count);
  if (unitId == 0)
  {
    Serial.println("audio hardware volume unsupported");
    return;
  }

  if (usb.audioHasMute(address, unitId))
  {
    // en: Some devices remember mute state, so clear mute before testing volume.
    // ja: mute状態を保持するデバイスがあるため、volume確認前にmuteを解除します。
    Serial.printf("audio mute %s\n", usb.audioSetMute(false, address, unitId) ? "off" : "failed");
  }

  if (!usb.audioHasVolume(address, unitId))
  {
    Serial.println("audio hardware volume unsupported");
    return;
  }

  EspUsbHostAudioVolumeRange range;
  if (usb.audioGetVolumeRange(range, address, unitId))
  {
    // en: USB Audio volume values are fixed-point dB values scaled by 256.
    // ja: USB Audioのvolume値は256倍された固定小数点dB値です。
    Serial.printf("audio volume range: %.2f..%.2f dB step %.2f dB\n",
                  range.min / 256.0f,
                  range.max / 256.0f,
                  range.resolution / 256.0f);
  }

  if (usb.audioSetVolumeDbClamped(USB_OUTPUT_VOLUME_DB, address, unitId))
  {
    float db = 0.0f;
    if (usb.audioGetVolumeDb(db, address, unitId))
    {
      Serial.printf("audio hardware volume: %.2f dB\n", db);
    }
  }
  else
  {
    Serial.println("audio hardware volume set failed");
  }
}

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
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost Audio Output Hardware Volume example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(info);
                          // en: Wait until the library has parsed usable USB Audio OUT interfaces.
                          // ja: ライブラリが利用可能なUSB Audio OUTインターフェースを解析するまで待ちます。
                          if (usb.audioOutputReady(info.address))
                          {
                            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                            const size_t count = usb.getAudioStreams(info.address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                            // en: Print every parsed stream, then select one matching the tone format.
                            // ja: 解析済みストリームをすべて表示し、トーン形式に合うものを選びます。
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
                                // en: Configure hardware volume only after the output stream is running.
                                // ja: 出力ストリーム開始後にハードウェアボリュームを設定します。
                                configureAudioOutputVolume(info.address);
                              }
                            }
                            Serial.printf("audio output %s: addr=%u\n", audioAddress == info.address ? "ready" : "unsupported", info.address);
                          } });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == audioAddress)
                             {
                               audioAddress = 0;
                             } });

  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           {
                             // en: Keep this callback short; it runs on the USB client task.
                             // ja: このコールバックはUSBクライアントタスク上で動くため、短時間で戻します。
                             fillTone(request);
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
