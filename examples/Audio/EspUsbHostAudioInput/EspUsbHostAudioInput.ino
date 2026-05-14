#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

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
