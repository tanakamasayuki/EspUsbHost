#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static constexpr uint32_t SAMPLE_RATE = 48000;
static uint32_t audioBytes = 0;
static uint32_t lastPrintMs = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHost Audio Input example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.printf("connected: addr=%u portId=0x%02x vid=%04x pid=%04x product=%s\n",
                                        info.address,
                                        info.portId,
                                        info.vid,
                                        info.pid,
                                        info.product);
                          if (usb.audioReady(info.address))
                          {
                            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                            const size_t count = usb.getAudioStreams(info.address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                            for (size_t i = 0; i < count; i++)
                            {
                              Serial.printf("audio stream: iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu rates=%u max=%u interval=%u\n",
                                            streams[i].interfaceNumber,
                                            streams[i].alternate,
                                            streams[i].endpointAddress,
                                            streams[i].input ? "IN" : "OUT",
                                            streams[i].channels,
                                            streams[i].bytesPerSample,
                                            streams[i].bitsPerSample,
                                            static_cast<unsigned long>(streams[i].sampleRate),
                                            streams[i].sampleRateCount,
                                            streams[i].maxPacketSize,
                                            streams[i].interval);
                            }
                            usb.setAudioSampleRate(SAMPLE_RATE, info.address);
                          }
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.printf("disconnected: addr=%u vid=%04x pid=%04x\n",
                                           info.address,
                                           info.vid,
                                           info.pid);
                           });

  usb.onAudioData([](const EspUsbHostAudioData &audio)
                  {
                    audioBytes += audio.length;
                    const uint32_t now = millis();
                    if (now - lastPrintMs >= 1000)
                    {
                      Serial.printf("audio: addr=%u iface=%u bytes_per_sec=%lu last_chunk=%u\n",
                                    audio.address,
                                    audio.interfaceNumber,
                                    static_cast<unsigned long>(audioBytes),
                                    static_cast<unsigned>(audio.length));
                      audioBytes = 0;
                      lastPrintMs = now;
                    }
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
