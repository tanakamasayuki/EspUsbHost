#include "EspUsbHost.h"

EspUsbHost usb;

static volatile bool connected = false;
static uint8_t deviceAddress = 0;
static volatile bool vendorDataSeen = false;
static char vendorData[64] = {};

static void printVendorInfo()
{
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];

  const size_t interfaceCount = usb.getInterfaces(deviceAddress, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  const size_t endpointCount = usb.getEndpoints(deviceAddress, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);

  bool vendorInterface = false;
  bool bulkOut = false;
  bool bulkIn = false;
  uint8_t vendorInterfaceNumber = 0xff;

  for (size_t i = 0; i < interfaceCount; i++)
  {
    const EspUsbHostInterfaceInfo &itf = interfaces[i];
    Serial.printf("INTERFACE number=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u claimed=%u attempted=%u claim=%s\n",
                  itf.number,
                  itf.interfaceClass,
                  itf.interfaceSubClass,
                  itf.interfaceProtocol,
                  itf.endpointCount,
                  itf.claimed ? 1 : 0,
                  itf.claimAttempted ? 1 : 0,
                  esp_err_to_name(itf.claimResult));
    if (itf.interfaceClass == 0xff && itf.endpointCount == 2)
    {
      vendorInterface = true;
      vendorInterfaceNumber = itf.number;
    }
  }

  for (size_t i = 0; i < endpointCount; i++)
  {
    const EspUsbHostEndpointInfo &ep = endpoints[i];
    const bool isBulk = (ep.attributes & 0x03) == 0x02;
    Serial.printf("ENDPOINT iface=%u ep=0x%02x attrs=0x%02x mps=%u interval=%u\n",
                  ep.interfaceNumber,
                  ep.address,
                  ep.attributes,
                  ep.maxPacketSize,
                  ep.interval);
    if (ep.interfaceNumber == vendorInterfaceNumber && isBulk)
    {
      if (ep.address & 0x80)
      {
        bulkIn = true;
      }
      else
      {
        bulkOut = true;
      }
    }
  }

  Serial.printf("VENDOR_ENUM interface=%u bulk_out=%u bulk_in=%u interfaces=%u endpoints=%u claimed_channels=%u managed=%u\n",
                vendorInterface ? 1 : 0,
                bulkOut ? 1 : 0,
                bulkIn ? 1 : 0,
                static_cast<unsigned>(interfaceCount),
                static_cast<unsigned>(endpointCount),
                static_cast<unsigned>(usb.endpointChannelCount(deviceAddress)),
                static_cast<unsigned>(usb.managedEndpointCount(deviceAddress)));
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          connected = true;
                          Serial.printf("HOST_CONNECTED address=%u vid=%04x pid=%04x supported=%u interfaces=%u\n",
                                        device.address,
                                        device.vid,
                                        device.pid,
                                        device.supported ? 1 : 0,
                                        device.configurationInterfaceCount);
                        });

  usb.onVendorData([](const EspUsbHostVendorData &data)
                   {
                     const size_t copyLen = data.length < sizeof(vendorData) - 1 ? data.length : sizeof(vendorData) - 1;
                     memcpy(vendorData, data.data, copyLen);
                     vendorData[copyLen] = '\0';
                     vendorDataSeen = true;
                   });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      if (connected)
      {
        printVendorInfo();
      }
      else
      {
        Serial.println("VENDOR_ENUM interface=0 bulk_out=0 bulk_in=0 interfaces=0 endpoints=0 claimed_channels=0 managed=0");
      }
    }
    else if (command == 'o')
    {
      Serial.printf("VENDOR_OPEN %u\n", usb.vendorOpen(deviceAddress) ? 1 : 0);
    }
    else if (command == 'w')
    {
      vendorDataSeen = false;
      memset(vendorData, 0, sizeof(vendorData));
      static const uint8_t payload[] = {'p', 'i', 'n', 'g'};
      Serial.printf("VENDOR_WRITE %u\n", usb.vendorWrite(payload, sizeof(payload), deviceAddress) ? 1 : 0);
    }
    else if (command == 'r')
    {
      uint8_t buffer[64];
      const size_t length = usb.vendorRead(buffer, sizeof(buffer) - 1, deviceAddress);
      buffer[length] = 0;
      Serial.printf("VENDOR_READ len=%u data=%s\n", static_cast<unsigned>(length), reinterpret_cast<const char *>(buffer));
    }
    else if (command == 'd')
    {
      Serial.printf("VENDOR_DATA seen=%u data=%s\n", vendorDataSeen ? 1 : 0, vendorData);
    }
    else if (command == 'p')
    {
      const uint32_t start = millis();
      while (!vendorDataSeen && millis() - start < 1000)
      {
        delay(1);
      }
      Serial.printf("VENDOR_DATA seen=%u data=%s\n", vendorDataSeen ? 1 : 0, vendorData);
    }
    else if (command == 'c')
    {
      uint8_t buffer[32] = {};
      size_t actual = 0;
      const bool inOk = usb.vendorControlIn(0x01, 0, 0, buffer, sizeof(buffer), &actual, deviceAddress);
      buffer[actual < sizeof(buffer) ? actual : sizeof(buffer) - 1] = 0;
      const bool outOk = usb.vendorControlOut(0x02, 0, 0, nullptr, 0, deviceAddress);
      Serial.printf("VENDOR_CONTROL in=%u len=%u data=%s out=%u\n",
                    inOk ? 1 : 0,
                    static_cast<unsigned>(actual),
                    reinterpret_cast<const char *>(buffer),
                    outOk ? 1 : 0);
    }
  }
  delay(1);
}
