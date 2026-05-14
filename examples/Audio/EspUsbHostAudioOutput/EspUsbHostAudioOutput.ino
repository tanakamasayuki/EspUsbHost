#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static uint8_t audioAddress = 0;
static bool audioReady = false;
static int16_t samples[48];

static void fillTone()
{
  static uint32_t phase = 0;
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
  {
    samples[i] = (phase < 24000) ? 12000 : -12000;
    phase += 440;
    if (phase >= 48000)
    {
      phase -= 48000;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHost Audio Output example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.printf("connected: addr=%u portId=0x%02x vid=%04x pid=%04x product=%s\n",
                                        info.address,
                                        info.portId,
                                        info.vid,
                                        info.pid,
                                        info.product);
                          if (usb.audioOutputReady(info.address))
                          {
                            audioAddress = info.address;
                            audioReady = true;
                            Serial.printf("audio output ready: addr=%u\n", info.address);
                          }
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.printf("disconnected: addr=%u vid=%04x pid=%04x\n",
                                           info.address,
                                           info.vid,
                                           info.pid);
                             if (info.address == audioAddress)
                             {
                               audioReady = false;
                               audioAddress = 0;
                             }
                           });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (audioReady)
  {
    fillTone();
    usb.audioSend(reinterpret_cast<const uint8_t *>(samples), sizeof(samples), audioAddress);
    delay(1);
  }
  else
  {
    delay(10);
  }
}
