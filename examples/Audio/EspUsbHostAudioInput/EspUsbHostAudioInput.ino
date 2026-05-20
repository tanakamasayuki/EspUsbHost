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
                          Serial.print("connected: ");
                          espUsbHostPrint(info);
                          if (usb.audioReady(info.address))
                          {
                            EspUsbHostAudioStreamInfo streams[ESP_USB_HOST_MAX_AUDIO_STREAMS];
                            const size_t count = usb.getAudioStreams(info.address, streams, ESP_USB_HOST_MAX_AUDIO_STREAMS);
                            for (size_t i = 0; i < count; i++)
                            {
                              espUsbHostPrint(streams[i]);
                            }
                            usb.setAudioSampleRate(SAMPLE_RATE, info.address);
                          } });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info); });

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
