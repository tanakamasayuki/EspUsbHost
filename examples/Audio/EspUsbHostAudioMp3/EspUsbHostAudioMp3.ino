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

// en: Ring buffer for decoded stereo PCM frames waiting to be sent (~32ms).
// ja: 送信待ちのデコード済みステレオPCMフレーム用リングバッファです（約32ms）。
static constexpr size_t PCM_BUF_FRAMES = FRAMES_PER_PACKET * 32;
static int16_t pcmBuf[PCM_BUF_FRAMES * CHANNELS];
static size_t pcmHead = 0;
static size_t pcmTail = 0;

static uint8_t audioAddress = 0;
static bool audioReady = false;

static int fileIndex = 0;
static AudioGeneratorMP3 *mp3gen = nullptr;
static AudioFileSourcePROGMEM *mp3src = nullptr;

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
      pcmBuf[pcmTail * CHANNELS + 0] = (int16_t)(((1.0f - t) * resamplePrev[0] + t * sample[LEFTCHANNEL]) * VOLUME);
      pcmBuf[pcmTail * CHANNELS + 1] = (int16_t)(((1.0f - t) * resamplePrev[1] + t * sample[RIGHTCHANNEL]) * VOLUME);
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
  delay(500);
  Serial.println("EspUsbHost Audio MP3 example start");

  if (!startNextFile())
    Serial.println("no MP3 files found in assets");

  usb.setAudioSampleRate(SAMPLE_RATE);

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
                              if (!audioReady &&
                                  streams[i].output &&
                                  streams[i].channels >= 1 &&
                                  streams[i].channels <= MAX_CHANNELS &&
                                  streams[i].bitsPerSample == BITS_PER_SAMPLE &&
                                  espUsbHostAudioStreamSupportsSampleRate(streams[i], SAMPLE_RATE))
                              {
                                audioAddress = info.address;
                                audioReady = true;
                                usb.setAudioSampleRate(SAMPLE_RATE, info.address);
                                usb.audioOutputStart(info.address);
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
                             } });

  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           { fillAudioOutput(request); });

  if (!usb.begin())
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
}

void loop()
{
  // en: Drive the MP3 decoder outside the USB callback; isRunning() stops at source exhaustion.
  // ja: MP3デコードはUSBコールバック外で進めます。isRunning()はソース終端でfalseになります。
  if (mp3gen && mp3gen->isRunning())
    mp3gen->loop();

  if (srcDone() && pcmAvail() < FRAMES_PER_PACKET)
  {
    if (!startNextFile())
    {
      Serial.println("all files played");
      audioReady = false;
      delay(10);
      return;
    }
  }
  delay(audioReady ? 1 : 10);
}
