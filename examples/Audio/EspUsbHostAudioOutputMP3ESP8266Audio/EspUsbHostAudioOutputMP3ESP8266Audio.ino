#include <Arduino.h>
#include <EspUsbHost.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include "assets_embed.h"

EspUsbHost usb;

static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr uint8_t CHANNELS = 2;
static constexpr uint8_t BYTES_PER_SAMPLE = 2;
static constexpr uint8_t BITS_PER_SAMPLE = 16;
static constexpr size_t MAX_CHANNELS = 2;
static constexpr size_t FRAMES_PER_PACKET = SAMPLE_RATE / 1000;
static constexpr float VOLUME = 1.0f; // 0.0-1.0
static constexpr uint8_t USB_OUTPUT_VOLUME_STEPS[] = {100, 50, 10, 0};

static bool isSupportedOutputStream(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample)
{
  // en: Change this function to choose which USB Audio OUT formats this sketch accepts.
  // ja: 受け入れるUSB Audio OUTフォーマットを変える場合は、この関数を変更します。
  return sampleRate == SAMPLE_RATE &&
         channels >= 1 &&
         channels <= MAX_CHANNELS &&
         bitsPerSample == BITS_PER_SAMPLE;
}

// en: Ring buffer for decoded stereo PCM frames waiting to be sent (~32ms).
// ja: 送信待ちのデコード済みステレオPCMフレーム用リングバッファです（約32ms）。
static constexpr size_t PCM_BUF_FRAMES = FRAMES_PER_PACKET * 32;
static int16_t pcmBuf[PCM_BUF_FRAMES * CHANNELS];
static size_t pcmHead = 0;
static size_t pcmTail = 0;

static uint8_t audioAddress = 0;
static bool audioReady = false;
static size_t volumeStepIndex = 0;
static bool playbackFinished = false;
static bool hardwareVolumeChecked = false;
static bool hardwareVolumeSupported = false;

static int fileIndex = 0;
static AudioGeneratorMP3 *mp3gen = nullptr;
static AudioFileSourcePROGMEM *mp3src = nullptr;

static float currentOutputGain()
{
  return hardwareVolumeSupported ? VOLUME : static_cast<float>(USB_OUTPUT_VOLUME_STEPS[volumeStepIndex]) / 100.0f;
}

static void configureAudioOutputVolume(uint8_t address)
{
  const uint8_t percent = USB_OUTPUT_VOLUME_STEPS[volumeStepIndex];
  if (hardwareVolumeChecked && !hardwareVolumeSupported)
  {
    Serial.printf("software gain: %.2f\n", currentOutputGain());
    return;
  }

  if (usb.audioConfigureVolumePercent(percent, address))
  {
    hardwareVolumeChecked = true;
    hardwareVolumeSupported = true;
    Serial.printf("audio hardware volume: %u%%\n", percent);
  }
  else
  {
    hardwareVolumeChecked = true;
    hardwareVolumeSupported = false;
    Serial.println("audio hardware volume unsupported");
    Serial.printf("software gain: %.2f\n", currentOutputGain());
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
  pcmHead = 0;
  pcmTail = 0;
  if (audioAddress)
  {
    configureAudioOutputVolume(audioAddress);
  }
  Serial.printf("restart playlist at %u%% volume\n", USB_OUTPUT_VOLUME_STEPS[volumeStepIndex]);
  return true;
}

static size_t pcmAvail()
{
  return (pcmTail + PCM_BUF_FRAMES - pcmHead) % PCM_BUF_FRAMES;
}

class RingBufferOutput : public AudioOutput
{
  float resamplePhase = 0.0f;
  int16_t resamplePrev[2] = {};

public:
  bool begin() override
  {
    resamplePhase = 0.0f;
    resamplePrev[0] = resamplePrev[1] = 0;
    return true;
  }

  bool SetRate(int hz) override
  {
    hertz = hz;
    resamplePhase = 0.0f;
    resamplePrev[0] = resamplePrev[1] = 0;
    return true;
  }

  // en: Linear interpolation resampler: hertz (decoded MP3 rate) -> SAMPLE_RATE (48000 Hz).
  // ja: 線形補間で、MP3のデコードレート hertz から SAMPLE_RATE（48000 Hz）へ変換します。
  bool ConsumeSample(int16_t sample[2]) override
  {
    const float step = (hertz > 0) ? (float)hertz / SAMPLE_RATE : 1.0f;
    while (resamplePhase < 1.0f)
    {
      const size_t next = (pcmTail + 1) % PCM_BUF_FRAMES;
      if (next == pcmHead)
        return false; // buffer full — generator retries this sample
      const float t = resamplePhase;
      const float gain = currentOutputGain();
      pcmBuf[pcmTail * CHANNELS + 0] = (int16_t)(((1.0f - t) * resamplePrev[0] + t * sample[LEFTCHANNEL]) * gain);
      pcmBuf[pcmTail * CHANNELS + 1] = (int16_t)(((1.0f - t) * resamplePrev[1] + t * sample[RIGHTCHANNEL]) * gain);
      pcmTail = next;
      resamplePhase += step;
    }
    resamplePhase -= 1.0f;
    resamplePrev[0] = sample[LEFTCHANNEL];
    resamplePrev[1] = sample[RIGHTCHANNEL];
    return true;
  }
};

static RingBufferOutput ringOut;

static bool srcDone()
{
  return mp3src == nullptr || mp3src->getPos() >= mp3src->getSize();
}

static bool startNextFile()
{
  // en: Release the previous decoder/source before opening the next embedded MP3.
  // ja: 次の埋め込みMP3を開く前に、前回のデコーダとソースを解放します。
  delete mp3gen;
  mp3gen = nullptr;
  delete mp3src;
  mp3src = nullptr;

  while (fileIndex < (int)assets_file_count)
  {
    const int idx = fileIndex++;
    const char *name = assets_file_names[idx];
    const size_t nameLen = strlen(name);
    if (nameLen >= 4 && strcmp(name + nameLen - 4, ".mp3") == 0)
    {
      mp3src = new AudioFileSourcePROGMEM(assets_file_data[idx], assets_file_sizes[idx]);
      mp3gen = new AudioGeneratorMP3();
      if (!mp3gen->begin(mp3src, &ringOut))
      {
        Serial.printf("begin failed: %s\n", name);
        delete mp3gen;
        mp3gen = nullptr;
        delete mp3src;
        mp3src = nullptr;
        continue;
      }
      Serial.printf("playing: %s (%u bytes)\n", name, (unsigned)assets_file_sizes[idx]);
      return true;
    }
  }
  return false;
}

static void fillAudioOutput(EspUsbHostAudioOutputRequest &request)
{
  // en: USB callback path: copy buffered PCM only; decoding runs from loop().
  // ja: USBコールバック経路ではバッファ済みPCMのコピーだけを行い、デコードはloop()で進めます。
  const size_t frames = min(pcmAvail(), request.frameCount);
  for (size_t f = 0; f < frames; f++)
  {
    const int16_t l = pcmBuf[pcmHead * CHANNELS + 0];
    const int16_t r = pcmBuf[pcmHead * CHANNELS + 1];
    pcmHead = (pcmHead + 1) % PCM_BUF_FRAMES;
    const size_t offset = f * request.channels * BYTES_PER_SAMPLE;
    request.data[offset + 0] = l & 0xff;
    request.data[offset + 1] = (l >> 8) & 0xff;
    if (request.channels > 1)
    {
      request.data[offset + 2] = r & 0xff;
      request.data[offset + 3] = (r >> 8) & 0xff;
    }
  }
  request.writtenFrames = frames;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost Audio Output MP3 ESP8266Audio example start");

  if (!startNextFile())
    Serial.println("no MP3 files found in assets");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(info);
                          if (usb.audioOutputReady(info.address))
                          {
                            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                            const size_t count = usb.getAudioStreams(info.address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                            // en: This example sends 48 kHz 16-bit PCM and accepts mono or stereo USB OUT streams.
                            // ja: このサンプルは48 kHz 16bit PCMを送信し、モノラルまたはステレオのUSB OUTストリームを使います。
                            for (size_t i = 0; i < count; i++)
                            {
                              espUsbHostPrint(streams[i]);
                            }
                            const EspUsbHostAudioStreamSelection selected = espUsbHostSelectAudioOutputStream(streams, count, isSupportedOutputStream);
                            if (!audioReady && selected)
                            {
                              if (usb.audioOutputStart(streams[selected.index], selected.sampleRate, info.address))
                              {
                                audioAddress = info.address;
                                audioReady = true;
                                configureAudioOutputVolume(info.address);
                              }
                            }
                            Serial.printf("audio output %s: addr=%u\n", audioReady ? "ready" : "unsupported", info.address);
                          } });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == audioAddress)
                             {
                               audioReady = false;
                               audioAddress = 0;
                               hardwareVolumeChecked = false;
                               hardwareVolumeSupported = false;
                             } });

  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           { fillAudioOutput(request); });

  if (!usb.begin())
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
}

void loop()
{
  if (playbackFinished)
  {
    delay(10);
    return;
  }

  // en: Drive the MP3 decoder outside the USB callback; isRunning() stops at source exhaustion.
  // ja: MP3デコードはUSBコールバック外で進めます。isRunning()はソース終端でfalseになります。
  if (mp3gen && mp3gen->isRunning())
    mp3gen->loop();

  if (srcDone() && pcmAvail() < FRAMES_PER_PACKET)
  {
    if (!startNextFile() && audioReady)
    {
      Serial.printf("playlist finished at %u%% volume\n", USB_OUTPUT_VOLUME_STEPS[volumeStepIndex]);
      if (nextVolumeLoop())
      {
        startNextFile();
      }
      delay(10);
      return;
    }
  }
  delay(audioReady ? 1 : 10);
}
