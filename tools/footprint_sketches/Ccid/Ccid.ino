#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  usb.onCcidCardInserted([](const EspUsbHostCcidSlotEvent &event)
                         { Serial.println(event.slot); });
  usb.begin();
  usb.ccidOpen();
}

void loop()
{
  EspUsbHostCcidStatus status;
  if (usb.ccidGetStatus(status) && status.present)
  {
    uint8_t atr[ESP_USB_HOST_CCID_MAX_ATR] = {};
    size_t atrLength = 0;
    if (usb.ccidPowerOn(atr, sizeof(atr), &atrLength))
    {
      EspUsbHostCcidCardInfo card;
      if (usb.ccidGetCardInfo(card))
      {
        Serial.println(card.standardText);
      }
      static const uint8_t getUid[] = {0xff, 0xca, 0x00, 0x00, 0x00};
      uint8_t response[32] = {};
      size_t responseLength = 0;
      uint16_t statusWord = 0;
      usb.ccidApdu(getUid, sizeof(getUid), response, sizeof(response), &responseLength, &statusWord);
      usb.ccidPowerOff();
    }
  }
  delay(1000);
}
