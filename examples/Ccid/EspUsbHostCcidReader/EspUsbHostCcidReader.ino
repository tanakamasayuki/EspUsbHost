#include "EspUsbHost.h"

// CCID smart card reader example.
//
// Opens the CCID interface of a connected reader, reports card insertion and
// removal, activates the card to read its ATR, and sends the PC/SC Get UID
// pseudo APDU (FF CA 00 00 00), which contactless readers answer with the card
// serial number.
//
// Slot-change callbacks run on the USB task, so they only set a flag here; the
// CCID commands themselves are issued from loop().

EspUsbHost usb;

static volatile bool cardPending = false;
static bool readerOpen = false;
static uint8_t readerAddress = 0;

static void printHex(const char *prefix, const uint8_t *data, size_t length)
{
  Serial.print(prefix);
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf("%02x", data[i]);
  }
  Serial.println();
}

static void openReader()
{
  if (readerOpen || !usb.ccidOpen())
  {
    return;
  }

  EspUsbHostCcidInterface info;
  if (!usb.ccidGetInterface(info))
  {
    return;
  }
  readerOpen = true;
  readerAddress = info.address;

  Serial.printf("CCID reader ready: address=%u interface=%u slots=%u exchange=%s interrupt=%s\n",
                info.address,
                info.interfaceNumber,
                info.slotCount,
                info.exchangeLevel == ESP_USB_HOST_CCID_EXCHANGE_EXTENDED_APDU  ? "extended APDU"
                : info.exchangeLevel == ESP_USB_HOST_CCID_EXCHANGE_SHORT_APDU   ? "short APDU"
                : info.exchangeLevel == ESP_USB_HOST_CCID_EXCHANGE_TPDU         ? "TPDU"
                                                                                : "character",
                info.interruptEndpoint ? "yes" : "no");

  // A card already sitting on the reader produces no insertion notification.
  if (usb.ccidCardPresent())
  {
    cardPending = true;
  }
}

static void readCard()
{
  uint8_t atr[ESP_USB_HOST_CCID_MAX_ATR] = {};
  size_t atrLength = 0;
  if (!usb.ccidPowerOn(atr, sizeof(atr), &atrLength))
  {
    Serial.printf("power on failed: error=0x%02x\n", usb.ccidLastError());
    return;
  }
  printHex("ATR: ", atr, atrLength);

  // The card standard and name come from the ATR: a PC/SC reader synthesizes
  // one for storage cards that carries both. A card with an ATR of its own
  // (contact cards, ISO 14443-4 cards) reports "ISO 7816 card (own ATR)", and a
  // card the ATR does not identify at all is looked up from its Get UID answer.
  EspUsbHostCcidCardInfo card;
  if (usb.ccidIdentifyCard(card))
  {
    Serial.printf("card: %s", card.standardText);
    if (card.level != 0)
    {
      Serial.printf(" level %u", card.level);
    }
    if (card.pcscStorageAtr)
    {
      Serial.printf(", %s (0x%04x)", card.cardNameText, card.cardName);
    }
    if (card.fromUid)
    {
      Serial.print(" (from UID ");
      for (uint8_t i = 0; i < card.uidLength; i++)
      {
        Serial.printf("%02x", card.uid[i]);
      }
      Serial.print(")");
    }
    Serial.println();
  }

  static const uint8_t getUid[] = {0xff, 0xca, 0x00, 0x00, 0x00};
  uint8_t response[64] = {};
  size_t responseLength = 0;
  uint16_t statusWord = 0;
  if (usb.ccidApdu(getUid, sizeof(getUid), response, sizeof(response), &responseLength, &statusWord))
  {
    Serial.printf("Get UID: sw=%04x\n", statusWord);
    printHex("UID: ", response, responseLength);
  }
  else
  {
    Serial.printf("Get UID failed: error=0x%02x\n", usb.ccidLastError());
  }

  usb.ccidPowerOff();
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected: address=%u vid=%04x pid=%04x product=\"%s\"\n",
                                        device.address, device.vid, device.pid, device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
    Serial.printf("disconnected: address=%u\n", device.address);
    if (device.address == readerAddress)
    {
      readerOpen = false;
      readerAddress = 0;
    } });

  usb.onCcidCardInserted([](const EspUsbHostCcidSlotEvent &event)
                         {
    Serial.printf("card inserted: address=%u slot=%u\n", event.address, event.slot);
    cardPending = true; });

  usb.onCcidCardRemoved([](const EspUsbHostCcidSlotEvent &event)
                        { Serial.printf("card removed: address=%u slot=%u\n", event.address, event.slot); });

  usb.begin();
  Serial.println("EspUsbHost CCID reader example start");
}

void loop()
{
  if (!readerOpen)
  {
    openReader();
  }

  if (readerOpen && cardPending)
  {
    cardPending = false;
    readCard();
  }

  delay(100);
}
