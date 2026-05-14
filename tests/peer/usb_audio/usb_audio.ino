#include "EspUsbHost.h"

EspUsbHost usb;

static uint32_t audioBytes = 0;
static bool audioReported = false;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
                              Serial.printf("DEVICE_CONNECTED addr=%u portId=0x%02x class=0x%02x\n",
                                            device.address,
                                            device.portId,
                                            device.deviceClass);
                              EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
                              const size_t interfaceCount = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
                              for (size_t i = 0; i < interfaceCount; i++)
                              {
                                  Serial.printf("INTERFACE number=%u alt=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u\n",
                                                interfaces[i].number,
                                                interfaces[i].alternate,
                                                interfaces[i].interfaceClass,
                                                interfaces[i].interfaceSubClass,
                                                interfaces[i].interfaceProtocol,
                                                interfaces[i].endpointCount);
                              }
                              EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
                              const size_t endpointCount = usb.getEndpoints(device.address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);
                              for (size_t i = 0; i < endpointCount; i++)
                              {
                                  Serial.printf("ENDPOINT iface=%u ep=0x%02x attrs=0x%02x max=%u interval=%u\n",
                                                endpoints[i].interfaceNumber,
                                                endpoints[i].address,
                                                endpoints[i].attributes,
                                                endpoints[i].maxPacketSize,
                                                endpoints[i].interval);
                              }
                          });

    usb.onAudioData([](const EspUsbHostAudioData &audio)
                    {
                        audioBytes += audio.length;
                        if (!audioReported && audioBytes >= 96)
                        {
                            audioReported = true;
                            Serial.printf("AUDIO_RX addr=%u iface=%u total=%lu last=%u\n",
                                          audio.address,
                                          audio.interfaceNumber,
                                          static_cast<unsigned long>(audioBytes),
                                          static_cast<unsigned>(audio.length));
                        }
                    });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'r')
        {
            audioBytes = 0;
            audioReported = false;
            Serial.println("AUDIO_RESET");
        }
    }
    delay(1);
}
