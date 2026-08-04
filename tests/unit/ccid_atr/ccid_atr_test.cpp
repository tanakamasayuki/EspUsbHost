// Host tests for the CCID ATR parser. src/EspUsbHostCcidAtr.h is deliberately
// free of Arduino and USB dependencies, so the production header is included
// directly and the shipped code itself is exercised.

#include "EspUsbHostCcidAtr.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
  if (!condition)
  {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

void checkEqual(unsigned long actual, unsigned long expected, const char *what)
{
  if (actual != expected)
  {
    printf("FAIL: %s (actual=0x%lx %lu, expected=0x%lx %lu)\n", what, actual, actual, expected,
           expected);
    failures++;
  }
}

void checkText(const char *actual, const char *expected, const char *what)
{
  if (strcmp(actual, expected) != 0)
  {
    printf("FAIL: %s (actual=\"%s\", expected=\"%s\")\n", what, actual, expected);
    failures++;
  }
}

// PC/SC synthetic ATR for a contactless storage card:
// 3b 8f 80 01 80 4f 0c a0 00 00 03 06 SS NN NN 00 00 00 00 TCK
std::vector<uint8_t> pcscAtr(uint8_t standard, uint16_t name)
{
  std::vector<uint8_t> atr = {0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
                              0x03, 0x06, standard, static_cast<uint8_t>(name >> 8),
                              static_cast<uint8_t>(name & 0xff), 0x00, 0x00, 0x00, 0x00};
  uint8_t tck = 0;
  for (size_t i = 1; i < atr.size(); i++)
  {
    tck = static_cast<uint8_t>(tck ^ atr[i]);
  }
  atr.push_back(tck);
  return atr;
}

void testMifareClassic1k()
{
  // Captured from a real Sony RC-S300 with an ISO 14443 A card on it
  // (tests/manual/ccid_card).
  const uint8_t atr[] = {0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
                         0x03, 0x06, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6a};
  EspUsbHostCcidCardInfo info;
  check(espUsbHostParseCcidAtr(atr, sizeof(atr), info), "real RC-S300 ATR parses");
  check(info.pcscStorageAtr, "real ATR is a PC/SC storage ATR");
  checkEqual(info.standard, ESP_USB_HOST_CCID_CARD_ISO_14443_A, "real ATR standard");
  checkEqual(info.standardCode, 0x03, "real ATR standard code");
  checkEqual(info.level, 3, "real ATR level");
  checkEqual(info.cardName, 0x0001, "real ATR card name");
  checkText(info.standardText, "ISO 14443 A", "real ATR standard text");
  checkText(info.cardNameText, "MIFARE Classic 1K", "real ATR card name text");
  // TD1 = 0x80 announces T=0, TD2 = 0x01 announces T=1.
  checkEqual(info.protocols, 0x03, "real ATR protocols");

  // The TCK of the captured ATR must match the one the helper computes, which
  // pins the test vector itself.
  const std::vector<uint8_t> generated = pcscAtr(0x03, 0x0001);
  check(generated.size() == sizeof(atr) && memcmp(generated.data(), atr, sizeof(atr)) == 0,
        "generated ATR matches the captured one");
}

void testStandards()
{
  struct Case
  {
    uint8_t code;
    EspUsbHostCcidCardStandard standard;
    uint8_t level;
    const char *text;
  };
  static const Case cases[] = {
      {0x01, ESP_USB_HOST_CCID_CARD_ISO_14443_A, 1, "ISO 14443 A"},
      {0x02, ESP_USB_HOST_CCID_CARD_ISO_14443_A, 2, "ISO 14443 A"},
      {0x03, ESP_USB_HOST_CCID_CARD_ISO_14443_A, 3, "ISO 14443 A"},
      {0x05, ESP_USB_HOST_CCID_CARD_ISO_14443_B, 1, "ISO 14443 B"},
      {0x06, ESP_USB_HOST_CCID_CARD_ISO_14443_B, 2, "ISO 14443 B"},
      {0x07, ESP_USB_HOST_CCID_CARD_ISO_14443_B, 3, "ISO 14443 B"},
      {0x09, ESP_USB_HOST_CCID_CARD_ISO_15693, 1, "ISO 15693"},
      {0x0c, ESP_USB_HOST_CCID_CARD_ISO_15693, 4, "ISO 15693"},
      {0x0d, ESP_USB_HOST_CCID_CARD_CONTACT_MEMORY, 0, "contact memory card (ISO 7816-10)"},
      {0x10, ESP_USB_HOST_CCID_CARD_CONTACT_MEMORY, 0, "contact memory card (ISO 7816-10)"},
      {0x11, ESP_USB_HOST_CCID_CARD_FELICA, 0, "FeliCa"},
      {0x40, ESP_USB_HOST_CCID_CARD_LOW_FREQUENCY, 0, "low frequency contactless"},
  };

  for (const Case &c : cases)
  {
    const std::vector<uint8_t> atr = pcscAtr(c.code, 0x0000);
    EspUsbHostCcidCardInfo info;
    check(espUsbHostParseCcidAtr(atr.data(), atr.size(), info), "standard case parses");
    checkEqual(info.standard, c.standard, "standard case standard");
    checkEqual(info.level, c.level, "standard case level");
    checkText(info.standardText, c.text, "standard case text");
    checkEqual(info.standardCode, c.code, "standard case raw code");
  }

  // Captured from a real Sony RC-S300 with a FeliCa card on it: standard 0x11,
  // name 0x003b (tests/manual/ccid_card).
  const uint8_t felica[] = {0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
                            0x03, 0x06, 0x11, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x00, 0x42};
  EspUsbHostCcidCardInfo info;
  check(espUsbHostParseCcidAtr(felica, sizeof(felica), info), "FeliCa ATR parses");
  check(info.pcscStorageAtr, "FeliCa ATR is a PC/SC storage ATR");
  checkEqual(info.standard, ESP_USB_HOST_CCID_CARD_FELICA, "FeliCa standard");
  checkEqual(info.standardCode, 0x11, "FeliCa standard code");
  checkEqual(info.level, 0, "FeliCa has no level");
  checkEqual(info.cardName, 0x003b, "FeliCa card name");
  checkText(info.cardNameText, "FeliCa", "FeliCa card name text");
  const std::vector<uint8_t> generatedFelica = pcscAtr(0x11, 0x003b);
  check(generatedFelica.size() == sizeof(felica) &&
            memcmp(generatedFelica.data(), felica, sizeof(felica)) == 0,
        "generated FeliCa ATR matches the captured one");

  // An unlisted standard byte must not be reported as an ISO 7816 card: the ATR
  // does identify a storage card, just not one this table knows.
  const std::vector<uint8_t> unlisted = pcscAtr(0x7f, 0x1234);
  check(espUsbHostParseCcidAtr(unlisted.data(), unlisted.size(), info), "unlisted standard parses");
  check(info.pcscStorageAtr, "unlisted standard is still a PC/SC ATR");
  checkEqual(info.standard, ESP_USB_HOST_CCID_CARD_UNKNOWN, "unlisted standard is unknown");
  checkEqual(info.standardCode, 0x7f, "unlisted standard raw code");
  checkEqual(info.cardName, 0x1234, "unlisted card name raw value");
  checkText(info.cardNameText, "unknown", "unlisted card name text");
}

void testOwnAtr()
{
  // A contact card's own ATR (T=0 and T=1, 8 historical bytes). No PC/SC card
  // identification, so it must come back as an ISO 7816 card, not as unknown.
  const uint8_t atr[] = {0x3b, 0xf8, 0x13, 0x00, 0x00, 0x81, 0x31, 0xfe, 0x45,
                         0x4a, 0x43, 0x4f, 0x50, 0x76, 0x32, 0x34, 0x31, 0xb7};
  EspUsbHostCcidCardInfo info;
  check(espUsbHostParseCcidAtr(atr, sizeof(atr), info), "own ATR parses");
  check(!info.pcscStorageAtr, "own ATR is not a PC/SC storage ATR");
  checkEqual(info.standard, ESP_USB_HOST_CCID_CARD_ISO_7816, "own ATR standard");
  checkEqual(info.standardCode, 0, "own ATR has no standard code");
  checkEqual(info.cardName, 0, "own ATR has no card name");
  // TD1 = 0x81 (T=1), TD2 = 0x31 (T=1) -> T=1 only.
  checkEqual(info.protocols, 0x02, "own ATR protocols");

  // Historical bytes are located after the interface bytes, not at a fixed
  // offset: TA1/TB1/TC1/TD1 present, then TA2/TD2.
  const uint8_t *historical = nullptr;
  size_t historicalLength = 0;
  uint8_t protocols = 0;
  check(espUsbHostCcidAtrHistoricalBytes(atr, sizeof(atr), &historical, &historicalLength, &protocols),
        "historical bytes located");
  checkEqual(historicalLength, 8, "historical byte count");
  check(historical != nullptr && memcmp(historical, "JCOPv241", 8) == 0, "historical byte content");

  // An ATR with no TD byte at all implies T=0.
  const uint8_t t0Only[] = {0x3b, 0x02, 0x41, 0x42};
  check(espUsbHostParseCcidAtr(t0Only, sizeof(t0Only), info), "T=0 only ATR parses");
  checkEqual(info.protocols, 0x01, "T=0 only ATR protocols");
  checkEqual(info.standard, ESP_USB_HOST_CCID_CARD_ISO_7816,
             "historical bytes that are not PC/SC still mean an ISO 7816 card");
}

void testNoIdentification()
{
  // A CCID reader answers with this for a FeliCa card (captured from a Sony
  // RC-S300 with an iPhone in Apple Pay / FeliCa mode): TS, T0 with no
  // historical bytes, TD1, TD2, TCK. There is nothing in it that identifies the
  // card, so it must not be reported as an ISO 7816 card.
  const uint8_t atr[] = {0x3b, 0x80, 0x80, 0x01, 0x01};
  EspUsbHostCcidCardInfo info;
  check(espUsbHostParseCcidAtr(atr, sizeof(atr), info), "FeliCa reader ATR parses");
  check(!info.pcscStorageAtr, "FeliCa reader ATR is not a PC/SC storage ATR");
  checkEqual(info.standard, ESP_USB_HOST_CCID_CARD_UNKNOWN, "ATR without historical bytes is unknown");
  checkText(info.standardText, "unknown", "ATR without historical bytes text");
  // TD1 = 0x80 (T=0), TD2 = 0x01 (T=1).
  checkEqual(info.protocols, 0x03, "FeliCa reader ATR protocols");
  check(!info.fromUid, "ATR result is not marked as coming from a UID");
  checkEqual(info.uidLength, 0, "ATR result carries no UID");
}

void testStandardFromUid()
{
  const uint8_t felica[] = {0x01, 0x01, 0x0b, 0x00, 0xf1, 0x22, 0x33, 0x44};
  checkEqual(espUsbHostCcidStandardFromUid(felica, sizeof(felica)), ESP_USB_HOST_CCID_CARD_FELICA,
             "8-byte identifier is a FeliCa IDm");

  const uint8_t iso15693[] = {0xe0, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  checkEqual(espUsbHostCcidStandardFromUid(iso15693, sizeof(iso15693)),
             ESP_USB_HOST_CCID_CARD_ISO_15693, "8-byte identifier starting with 0xe0 is ISO 15693");

  const uint8_t nfcid1_7[] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  checkEqual(espUsbHostCcidStandardFromUid(nfcid1_7, sizeof(nfcid1_7)),
             ESP_USB_HOST_CCID_CARD_ISO_14443_A, "7-byte identifier is ISO 14443 A");

  const uint8_t nfcid1_10[] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
  checkEqual(espUsbHostCcidStandardFromUid(nfcid1_10, sizeof(nfcid1_10)),
             ESP_USB_HOST_CCID_CARD_ISO_14443_A, "10-byte identifier is ISO 14443 A");

  // 4 bytes is an NFCID1 on type A and a PUPI on type B; the type stays open.
  const uint8_t four[] = {0x6b, 0x6d, 0xcc, 0xae};
  checkEqual(espUsbHostCcidStandardFromUid(four, sizeof(four)), ESP_USB_HOST_CCID_CARD_ISO_14443,
             "4-byte identifier leaves the ISO 14443 type open");

  // ISO 14443-3 reserves a leading 0x08 for a random NFCID1, which is what a
  // phone emulating a card presents. Captured from an iPhone on a Sony RC-S300.
  const uint8_t randomNfcid1[] = {0x08, 0x39, 0x1e, 0xaf};
  checkEqual(espUsbHostCcidStandardFromUid(randomNfcid1, sizeof(randomNfcid1)),
             ESP_USB_HOST_CCID_CARD_ISO_14443_A, "4-byte identifier starting with 0x08 is ISO 14443 A");
  checkText(espUsbHostCcidCardStandardText(ESP_USB_HOST_CCID_CARD_ISO_14443),
            "ISO 14443 (type A or B)", "open ISO 14443 text");

  const uint8_t five[] = {0x01, 0x02, 0x03, 0x04, 0x05};
  checkEqual(espUsbHostCcidStandardFromUid(five, sizeof(five)), ESP_USB_HOST_CCID_CARD_UNKNOWN,
             "identifier of an unexpected length is unknown");
  checkEqual(espUsbHostCcidStandardFromUid(nullptr, 8), ESP_USB_HOST_CCID_CARD_UNKNOWN,
             "null identifier is unknown");
}

void testRejects()
{
  EspUsbHostCcidCardInfo info;
  check(!espUsbHostParseCcidAtr(nullptr, 0, info), "null ATR rejected");

  const uint8_t empty[] = {0x3b};
  check(!espUsbHostParseCcidAtr(empty, sizeof(empty), info), "ATR without T0 rejected");

  const uint8_t badTs[] = {0x3c, 0x00};
  check(!espUsbHostParseCcidAtr(badTs, sizeof(badTs), info), "invalid TS rejected");

  // T0 promises 5 historical bytes that are not there.
  const uint8_t truncated[] = {0x3b, 0x05, 0x41, 0x42};
  check(!espUsbHostParseCcidAtr(truncated, sizeof(truncated), info),
        "truncated historical bytes rejected");

  // TD1 present but the byte is missing.
  const uint8_t truncatedTd[] = {0x3b, 0x80};
  check(!espUsbHostParseCcidAtr(truncatedTd, sizeof(truncatedTd), info), "missing TD1 rejected");

  // Right shape, wrong RID: not a PC/SC identification.
  std::vector<uint8_t> wrongRid = pcscAtr(0x03, 0x0001);
  wrongRid[7] = 0xa1;
  check(espUsbHostParseCcidAtr(wrongRid.data(), wrongRid.size(), info), "wrong RID still parses");
  check(!info.pcscStorageAtr, "wrong RID is not a PC/SC storage ATR");
  checkEqual(info.standard, ESP_USB_HOST_CCID_CARD_ISO_7816, "wrong RID falls back to ISO 7816");

  // A PC/SC-looking TLV whose length does not fit in the historical bytes.
  const uint8_t shortTlv[] = {0x3b, 0x86, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00};
  check(espUsbHostParseCcidAtr(shortTlv, sizeof(shortTlv), info), "short TLV still parses");
  check(!info.pcscStorageAtr, "short TLV is not accepted as a PC/SC ATR");
}

} // namespace

int main()
{
  testMifareClassic1k();
  testStandards();
  testOwnAtr();
  testNoIdentification();
  testStandardFromUid();
  testRejects();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all CCID ATR checks passed\n");
  return 0;
}
