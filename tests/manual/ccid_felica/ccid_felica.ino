#include "EspUsbHost.h"

#include "../../../examples/Ccid/EspUsbHostCcidFelicaIdm/Rcs300Device.hpp"

// Verifies the FeliCa IDm path the EspUsbHostCcidFelicaIdm example is built on:
// a Sony RC-S300 transparent session, a FeliCa Polling with an explicit System
// Code, and the IDm out of the answer.
//
// The reader's own polling is run first (IccPowerOn plus the PC/SC Get UID
// pseudo APDU) so the log says what is on the reader before the transparent
// session touches the field. That is the difference the example exists for: the
// reader's own poll reports whatever answers the wildcard, while the Polling
// below asks for one system.

EspUsbHost usb;
rcs300::Rcs300Reader reader(usb);

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

// What the reader finds on its own, for comparison with the Polling below.
static void reportReaderSideCard()
{
    uint8_t atr[ESP_USB_HOST_CCID_MAX_ATR] = {};
    size_t atrLength = 0;
    if (!usb.ccidPowerOn(atr, sizeof(atr), &atrLength) || atrLength == 0)
    {
        Serial.println("READER_CARD none");
        return;
    }
    printHex("READER_ATR data=", atr, atrLength);

    EspUsbHostCcidCardInfo card;
    if (usb.ccidGetCardInfo(card))
    {
        Serial.printf("READER_CARD standard=\"%s\" name=\"%s\" nameCode=0x%04x\n",
                      card.standardText,
                      card.cardNameText,
                      card.cardName);
    }

    static const uint8_t getUid[] = {0xff, 0xca, 0x00, 0x00, 0x00};
    uint8_t response[32] = {};
    size_t responseLength = 0;
    uint16_t statusWord = 0;
    if (usb.ccidApdu(getUid, sizeof(getUid), response, sizeof(response), &responseLength, &statusWord) &&
        statusWord == 0x9000)
    {
        printHex("READER_UID data=", response, responseLength);
    }
    usb.ccidPowerOff();
}

static bool pollAndReport(const char *label, uint16_t systemCode, felica::Target &target)
{
    if (reader.readTarget(systemCode, target))
    {
        Serial.printf("%s sc=%04x idm=", label, systemCode);
        for (size_t i = 0; i < sizeof(target.idm); i++)
        {
            Serial.printf("%02x", target.idm[i]);
        }
        Serial.printf(" pmm=");
        for (size_t i = 0; i < sizeof(target.pmm); i++)
        {
            Serial.printf("%02x", target.pmm[i]);
        }
        Serial.printf(" requestData=%u answeringSystemCode=%04x\n",
                      target.hasRequestData ? 1 : 0,
                      target.requestData);
        return true;
    }
    Serial.printf("%s sc=%04x %s result=0x%02x sw=%04x\n",
                  label,
                  systemCode,
                  reader.noAnswer() ? "no answer" : "failed",
                  reader.lastResult(),
                  reader.lastStatusWord());
    return false;
}

static void runTest()
{
    if (!reader.open())
    {
        Serial.println("RCS300_OPEN failed");
        Serial.println("[FAIL]");
        Serial.println("No Sony RC-S300 (054c:0dc8) with a CCID interface was found.");
        return;
    }
    Serial.printf("RCS300_OPEN address=%u\n", reader.address());

    // The transparent session commands are checked one at a time, so a failure
    // names the step that broke rather than just "no card".
    if (!reader.startSession())
    {
        Serial.printf("RCS300_SESSION failed result=0x%02x sw=%04x\n",
                      reader.lastResult(),
                      reader.lastStatusWord());
        Serial.println("[FAIL]");
        return;
    }
    Serial.println("RCS300_SESSION ok");

    if (!reader.selectFelica())
    {
        Serial.printf("RCS300_SWITCH failed result=0x%02x sw=%04x\n",
                      reader.lastResult(),
                      reader.lastStatusWord());
        reader.endSession();
        Serial.println("[FAIL]");
        return;
    }
    // The reader normally names no protocol here, so 0x00 is the expected value.
    Serial.printf("RCS300_SWITCH ok protocol=0x%02x\n", reader.selectedProtocol());

    if (!reader.rfOn() || !reader.rfOff())
    {
        Serial.printf("RCS300_RF failed result=0x%02x sw=%04x\n",
                      reader.lastResult(),
                      reader.lastStatusWord());
        reader.endSession();
        Serial.println("[FAIL]");
        return;
    }
    Serial.println("RCS300_RF ok");
    reader.endSession();

    Serial.println("Place a FeliCa card (Suica etc.) on the reader and keep it there.");
    felica::Target wildcard;
    felica::Target transit;
    bool gotWildcard = false;
    const uint32_t waitUntil = millis() + CARD_WAIT_MS;
    while (!gotWildcard && (int32_t)(waitUntil - millis()) > 0)
    {
        gotWildcard = reader.readTarget(felica::SYSTEM_CODE_ANY, wildcard);
        if (!gotWildcard)
        {
            delay(300);
        }
    }

    reportReaderSideCard();

    if (!gotWildcard)
    {
        Serial.println("FELICA_WILDCARD sc=ffff no answer");
        Serial.println("[FAIL]");
        Serial.println("No FeliCa target answered the wildcard Polling within the timeout.");
        return;
    }
    pollAndReport("FELICA_WILDCARD", felica::SYSTEM_CODE_ANY, wildcard);

    const bool gotTransit = pollAndReport("FELICA_TRANSIT", felica::SYSTEM_CODE_TRANSIT, transit);

    // An IDm has to be stable across polls, otherwise it is not an identifier.
    felica::Target again;
    if (pollAndReport("FELICA_REPEAT", felica::SYSTEM_CODE_ANY, again) &&
        memcmp(again.idm, wildcard.idm, sizeof(again.idm)) != 0)
    {
        Serial.println("FELICA_REPEAT IDm changed between polls");
        Serial.println("[FAIL]");
        return;
    }

    // A card that is not a transit card answers the wildcard and not 0x0003, which
    // is a pass for the mechanism: the System Code filtered, which is the point.
    if (!gotTransit)
    {
        Serial.println("[PASS]");
        Serial.println("The wildcard Polling found a target and 0x0003 did not, so the card holds no transit system.");
        return;
    }

    if (memcmp(transit.idm, wildcard.idm, sizeof(transit.idm)) == 0)
    {
        Serial.println("FELICA_COMPARE same IDm for both System Codes");
    }
    else
    {
        Serial.println("FELICA_COMPARE different IDm per System Code");
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
    Serial.println("ccid_felica test start");
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
