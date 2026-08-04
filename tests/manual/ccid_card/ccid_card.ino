#include "EspUsbHost.h"

// Exercises the CCID API against a real reader and card: open the interface,
// read the slot status, activate the card and print its ATR, then send the
// PC/SC pseudo APDU FF CA 00 00 00 (Get UID).

EspUsbHost usb;

static constexpr uint32_t TEST_TIMEOUT_MS = 30000;
static constexpr uint32_t CARD_WAIT_MS = 30000;
static constexpr uint32_t SETTLE_MS = 1500;

static bool finished = false;
static uint32_t lastDeviceEventMs = 0;

static void printHex(const char *prefix, const uint8_t *data, size_t length)
{
    Serial.print(prefix);
    for (size_t i = 0; i < length; i++)
    {
        Serial.printf("%02x", data[i]);
    }
    Serial.println();
}

static const char *iccStatusName(EspUsbHostCcidIccStatus status)
{
    switch (status)
    {
    case ESP_USB_HOST_CCID_ICC_ACTIVE:
        return "active";
    case ESP_USB_HOST_CCID_ICC_INACTIVE:
        return "inactive";
    case ESP_USB_HOST_CCID_ICC_ABSENT:
        return "absent";
    default:
        return "unknown";
    }
}

static void runTest()
{
    if (!usb.ccidOpen())
    {
        Serial.println("CCID_OPEN failed");
        Serial.println("[FAIL]");
        return;
    }

    EspUsbHostCcidInterface info;
    if (!usb.ccidGetInterface(info))
    {
        Serial.println("CCID_INTERFACE unavailable");
        Serial.println("[FAIL]");
        return;
    }
    Serial.printf("CCID_INTERFACE address=%u iface=%u in=0x%02x out=0x%02x interrupt=0x%02x classDesc=%u bcd=%04x slots=%u voltage=0x%02x protocols=0x%08lx features=0x%08lx maxMessage=%lu exchange=%u\n",
                  info.address,
                  info.interfaceNumber,
                  info.inEndpoint,
                  info.outEndpoint,
                  info.interruptEndpoint,
                  info.hasClassDescriptor ? 1 : 0,
                  info.bcdCCID,
                  info.slotCount,
                  info.voltageSupport,
                  (unsigned long)info.protocols,
                  (unsigned long)info.features,
                  (unsigned long)info.maxMessageLength,
                  (unsigned)info.exchangeLevel);

    // Wait for a card so the operator can hold a phone (Apple Pay / FeliCa) on
    // the reader after the test has started.
    Serial.println("Place a card on the reader and keep it there.");
    EspUsbHostCcidStatus status;
    const uint32_t waitUntil = millis() + CARD_WAIT_MS;
    while (true)
    {
        if (!usb.ccidGetStatus(status))
        {
            Serial.println("CCID_STATUS failed");
            Serial.println("[FAIL]");
            return;
        }
        if (status.present || (int32_t)(waitUntil - millis()) <= 0)
        {
            break;
        }
        delay(200);
    }
    Serial.printf("CCID_STATUS slot=%u icc=%s present=%u active=%u command=%u error=0x%02x\n",
                  status.slot,
                  iccStatusName(status.iccStatus),
                  status.present ? 1 : 0,
                  status.active ? 1 : 0,
                  (unsigned)status.commandStatus,
                  status.error);

    if (!status.present)
    {
        Serial.println("CCID_CARD absent");
        Serial.println("[FAIL]");
        Serial.println("No card was placed on the reader within the timeout.");
        return;
    }

    uint8_t atr[ESP_USB_HOST_CCID_MAX_ATR] = {};
    size_t atrLength = 0;
    if (!usb.ccidPowerOn(atr, sizeof(atr), &atrLength))
    {
        Serial.printf("CCID_POWER_ON failed error=0x%02x\n", usb.ccidLastError());
        Serial.println("[FAIL]");
        return;
    }
    Serial.printf("CCID_ATR length=%u\n", (unsigned)atrLength);
    printHex("CCID_ATR data=", atr, atrLength);

    EspUsbHostCcidCardInfo card;
    if (!usb.ccidGetCardInfo(card))
    {
        Serial.println("CCID_CARD info unavailable");
        usb.ccidPowerOff();
        Serial.println("[FAIL]");
        return;
    }
    Serial.printf("CCID_CARD standard=\"%s\" code=0x%02x level=%u name=\"%s\" nameCode=0x%04x pcsc=%u protocols=0x%02x\n",
                  card.standardText,
                  card.standardCode,
                  card.level,
                  card.cardNameText,
                  card.cardName,
                  card.pcscStorageAtr ? 1 : 0,
                  card.protocols);

    // Cards the ATR does not identify (FeliCa, for one) are identified from the
    // Get UID answer instead.
    EspUsbHostCcidCardInfo identified;
    if (!usb.ccidIdentifyCard(identified))
    {
        Serial.println("CCID_IDENTIFY failed");
        usb.ccidPowerOff();
        Serial.println("[FAIL]");
        return;
    }
    Serial.printf("CCID_IDENTIFY standard=\"%s\" fromUid=%u uidLength=%u\n",
                  identified.standardText,
                  identified.fromUid ? 1 : 0,
                  identified.uidLength);
    printHex("CCID_IDENTIFY uid=", identified.uid, identified.uidLength);
    if (identified.standard == ESP_USB_HOST_CCID_CARD_UNKNOWN)
    {
        Serial.println("CCID_IDENTIFY could not determine the card standard");
        usb.ccidPowerOff();
        Serial.println("[FAIL]");
        return;
    }

    // PC/SC pseudo APDU understood by contactless readers: return the card UID.
    // Repeated so that bSeq handling over several commands is covered, and the
    // UID must not change between repeats.
    static const uint8_t getUid[] = {0xff, 0xca, 0x00, 0x00, 0x00};
    uint8_t response[64] = {};
    size_t responseLength = 0;
    uint16_t statusWord = 0;
    uint8_t firstUid[64] = {};
    size_t firstUidLength = 0;
    for (int attempt = 0; attempt < 3; attempt++)
    {
        if (!usb.ccidApdu(getUid,
                          sizeof(getUid),
                          response,
                          sizeof(response),
                          &responseLength,
                          &statusWord))
        {
            Serial.printf("CCID_APDU failed attempt=%d error=0x%02x\n", attempt, usb.ccidLastError());
            usb.ccidPowerOff();
            Serial.println("[FAIL]");
            return;
        }
        Serial.printf("CCID_APDU attempt=%d sw=%04x length=%u\n", attempt, statusWord, (unsigned)responseLength);
        printHex("CCID_UID data=", response, responseLength);

        if (attempt == 0)
        {
            firstUidLength = responseLength;
            memcpy(firstUid, response, responseLength);
        }
        else if (responseLength != firstUidLength || memcmp(firstUid, response, responseLength) != 0)
        {
            Serial.println("CCID_UID mismatch between repeats");
            usb.ccidPowerOff();
            Serial.println("[FAIL]");
            return;
        }
    }

    // A raw GetSlotStatus after the APDUs checks that the reader is still in
    // sync with the host's bSeq counter.
    EspUsbHostCcidResponse raw;
    if (!usb.ccidMessage(0x65, nullptr, nullptr, 0, raw))
    {
        Serial.println("CCID_MESSAGE GetSlotStatus failed");
        usb.ccidPowerOff();
        Serial.println("[FAIL]");
        return;
    }
    Serial.printf("CCID_MESSAGE type=0x%02x seq=%u status=0x%02x error=0x%02x length=%u\n",
                  raw.messageType,
                  raw.sequence,
                  raw.status,
                  raw.error,
                  (unsigned)raw.length);

    if (!usb.ccidPowerOff())
    {
        Serial.println("CCID_POWER_OFF failed");
    }

    if (statusWord != 0x9000)
    {
        Serial.println("[FAIL]");
        Serial.println("The reader answered, but Get UID returned a non-9000 status word.");
        return;
    }
    Serial.println("[PASS]");
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { lastDeviceEventMs = millis();
                            Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                                          device.address, device.vid, device.pid, device.product); });

    usb.begin();
    lastDeviceEventMs = millis();
    Serial.println("ccid_card test start");
}

void loop()
{
    static uint32_t startedAt = millis();

    if (!finished && millis() - lastDeviceEventMs >= SETTLE_MS && millis() - startedAt >= 3000)
    {
        finished = true;
        runTest();
    }

    if (!finished && millis() - startedAt > TEST_TIMEOUT_MS)
    {
        finished = true;
        Serial.println("[FAIL]");
        Serial.println("No CCID reader was detected.");
    }

    delay(10);
}
