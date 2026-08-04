#ifndef ESP_USB_HOST_CCID_ATR_H
#define ESP_USB_HOST_CCID_ATR_H

// ATR parsing for CCID smart cards, kept free of Arduino and USB dependencies
// so it compiles on the host and is covered by tests/unit/ccid_atr.
//
// A CCID reader answers IccPowerOn with the card's ATR. Contactless storage
// cards have no ATR of their own, so a PC/SC compliant reader synthesizes one
// whose historical bytes carry the PC/SC Workgroup RID (A0 00 00 03 06)
// followed by a standard byte (ISO 14443 A/B, ISO 15693, FeliCa, ...) and a
// card name. Cards that do answer for themselves (contact cards, and
// contactless cards that speak ISO 14443-4) return their own ATR instead, which
// carries no such identification.

#include <stddef.h>
#include <stdint.h>

enum EspUsbHostCcidCardStandard : uint8_t
{
  ESP_USB_HOST_CCID_CARD_UNKNOWN = 0,
  ESP_USB_HOST_CCID_CARD_ISO_14443_A,
  ESP_USB_HOST_CCID_CARD_ISO_14443_B,
  // ISO 14443 with the type left open: a 4-byte identifier is an NFCID1 on
  // type A and a PUPI on type B, and the two cannot be told apart by length.
  ESP_USB_HOST_CCID_CARD_ISO_14443,
  ESP_USB_HOST_CCID_CARD_ISO_15693,
  ESP_USB_HOST_CCID_CARD_FELICA,
  ESP_USB_HOST_CCID_CARD_LOW_FREQUENCY,
  // ISO 7816-10 memory cards on a contact reader (I2C / 2WBP / 3WBP).
  ESP_USB_HOST_CCID_CARD_CONTACT_MEMORY,
  // The card answered with historical bytes of its own: a contact ISO 7816
  // card, or a contactless card that speaks ISO 14443-4. The card type is not
  // encoded in the ATR, so it is not narrowed further here.
  ESP_USB_HOST_CCID_CARD_ISO_7816,
};

struct EspUsbHostCcidCardInfo
{
  EspUsbHostCcidCardStandard standard = ESP_USB_HOST_CCID_CARD_UNKNOWN;
  // True when the ATR is the PC/SC synthetic one for a storage card, which is
  // what makes standardCode / cardName meaningful.
  bool pcscStorageAtr = false;
  uint8_t standardCode = 0; // PC/SC PIX.SS
  uint16_t cardName = 0;    // PC/SC PIX.Name
  // ISO 14443 / ISO 15693 level (1..4) the reader reports, 0 when not applicable.
  uint8_t level = 0;
  // Protocols the ATR's TD bytes announce: bit 0 = T=0, bit 1 = T=1.
  uint8_t protocols = 0;
  const char *standardText = "unknown";
  // Name for a known cardName, "unknown" otherwise. The raw value stays in
  // cardName for cards the table does not list.
  const char *cardNameText = "unknown";
  // Set by ccidIdentifyCard() when the ATR said nothing and the standard was
  // inferred from the card identifier instead. See
  // espUsbHostCcidStandardFromUid() for how reliable that is.
  bool fromUid = false;
  uint8_t uid[10] = {};
  uint8_t uidLength = 0;
};

// Infers the card standard from the identifier a PC/SC Get UID
// (FF CA 00 00 00) returns. This is a heuristic on the identifier's shape, not
// a statement by the card:
//   - 8 bytes starting with 0xe0: an ISO 15693 UID, which always does
//   - 8 bytes otherwise: a FeliCa IDm
//   - 7 or 10 bytes: an ISO 14443 A NFCID1 (type B identifiers are 4 bytes)
//   - 4 bytes starting with 0x08: an ISO 14443 A random NFCID1, which ISO
//     14443-3 reserves that prefix for and phones emulating a card use
//   - 4 bytes otherwise: ISO 14443, type left open (NFCID1 on A, PUPI on B)
// Anything else stays unknown.
inline EspUsbHostCcidCardStandard espUsbHostCcidStandardFromUid(const uint8_t *uid, size_t length)
{
  if (!uid)
  {
    return ESP_USB_HOST_CCID_CARD_UNKNOWN;
  }
  switch (length)
  {
  case 4:
    return uid[0] == 0x08 ? ESP_USB_HOST_CCID_CARD_ISO_14443_A : ESP_USB_HOST_CCID_CARD_ISO_14443;
  case 7:
  case 10:
    return ESP_USB_HOST_CCID_CARD_ISO_14443_A;
  case 8:
    return uid[0] == 0xe0 ? ESP_USB_HOST_CCID_CARD_ISO_15693 : ESP_USB_HOST_CCID_CARD_FELICA;
  default:
    return ESP_USB_HOST_CCID_CARD_UNKNOWN;
  }
}

inline const char *espUsbHostCcidCardStandardText(EspUsbHostCcidCardStandard standard)
{
  switch (standard)
  {
  case ESP_USB_HOST_CCID_CARD_ISO_14443_A:
    return "ISO 14443 A";
  case ESP_USB_HOST_CCID_CARD_ISO_14443_B:
    return "ISO 14443 B";
  case ESP_USB_HOST_CCID_CARD_ISO_14443:
    return "ISO 14443 (type A or B)";
  case ESP_USB_HOST_CCID_CARD_ISO_15693:
    return "ISO 15693";
  case ESP_USB_HOST_CCID_CARD_FELICA:
    return "FeliCa";
  case ESP_USB_HOST_CCID_CARD_LOW_FREQUENCY:
    return "low frequency contactless";
  case ESP_USB_HOST_CCID_CARD_CONTACT_MEMORY:
    return "contact memory card (ISO 7816-10)";
  case ESP_USB_HOST_CCID_CARD_ISO_7816:
    return "ISO 7816 card (own ATR)";
  default:
    return "unknown";
  }
}

// PC/SC PIX.Name values. Only entries that are unambiguous in the PC/SC
// Workgroup list are named; anything else is left to the raw cardName.
inline const char *espUsbHostCcidCardNameText(uint16_t cardName)
{
  switch (cardName)
  {
  case 0x0000:
    return "no information";
  case 0x0001:
    return "MIFARE Classic 1K";
  case 0x0002:
    return "MIFARE Classic 4K";
  case 0x0003:
    return "MIFARE Ultralight";
  case 0x0014:
    return "ICODE SLI";
  case 0x0016:
    return "ICODE1";
  case 0x0021:
    return "LRI64";
  case 0x0022:
    return "ICODE UID";
  case 0x0023:
    return "ICODE EPC";
  case 0x0026:
    return "MIFARE Mini";
  case 0x0036:
    return "MIFARE Plus SL1 2K";
  case 0x0037:
    return "MIFARE Plus SL1 4K";
  case 0x0038:
    return "MIFARE Plus SL2 2K";
  case 0x0039:
    return "MIFARE Plus SL2 4K";
  case 0x003a:
    return "MIFARE Ultralight C";
  case 0x003b:
    return "FeliCa";
  default:
    return "unknown";
  }
}

// Locates the historical bytes of an ATR by walking its interface bytes, and
// collects the protocols the TD bytes announce. Returns false for an ATR that
// is truncated or has an invalid TS.
inline bool espUsbHostCcidAtrHistoricalBytes(const uint8_t *atr,
                                             size_t length,
                                             const uint8_t **historical,
                                             size_t *historicalLength,
                                             uint8_t *protocols)
{
  if (historical)
  {
    *historical = nullptr;
  }
  if (historicalLength)
  {
    *historicalLength = 0;
  }
  if (protocols)
  {
    *protocols = 0;
  }
  // TS is 0x3b (direct convention) or 0x3f (inverse convention), then T0.
  if (!atr || length < 2 || (atr[0] != 0x3b && atr[0] != 0x3f))
  {
    return false;
  }

  const uint8_t t0 = atr[1];
  const size_t historicalCount = t0 & 0x0f;
  uint8_t present = static_cast<uint8_t>(t0 >> 4);
  size_t offset = 2;
  uint8_t found = 0;

  while (true)
  {
    // TA, TB and TC only take up space; TD carries the next presence map in its
    // high nibble and one supported protocol in its low nibble.
    for (uint8_t bit = 0; bit < 3; bit++)
    {
      if (present & (1u << bit))
      {
        offset++;
      }
    }
    if (!(present & 0x08))
    {
      break;
    }
    if (offset >= length)
    {
      return false;
    }
    const uint8_t td = atr[offset++];
    const uint8_t protocol = static_cast<uint8_t>(td & 0x0f);
    if (protocol < 8)
    {
      found = static_cast<uint8_t>(found | (1u << protocol));
    }
    present = static_cast<uint8_t>(td >> 4);
  }

  if (offset + historicalCount > length)
  {
    return false;
  }
  // T=0 is implied when no TD byte named a protocol.
  if (found == 0)
  {
    found = 0x01;
  }
  if (protocols)
  {
    *protocols = found;
  }
  if (historical)
  {
    *historical = atr + offset;
  }
  if (historicalLength)
  {
    *historicalLength = historicalCount;
  }
  return true;
}

// Fills info from an ATR. Returns false when the ATR cannot be parsed at all;
// a parsable ATR with no PC/SC card identification still returns true, with
// standard set to ESP_USB_HOST_CCID_CARD_ISO_7816.
inline bool espUsbHostParseCcidAtr(const uint8_t *atr, size_t length, EspUsbHostCcidCardInfo &info)
{
  info = EspUsbHostCcidCardInfo();

  const uint8_t *historical = nullptr;
  size_t historicalLength = 0;
  uint8_t protocols = 0;
  if (!espUsbHostCcidAtrHistoricalBytes(atr, length, &historical, &historicalLength, &protocols))
  {
    return false;
  }
  info.protocols = protocols;
  // Historical bytes are the only place an ATR can identify the card. With none
  // at all the ATR says nothing -- which is what a CCID reader answers for a
  // FeliCa card, for instance -- so this must stay unknown rather than claim an
  // ISO 7816 card. ccidIdentifyCard() can then ask the card itself.
  info.standard = historicalLength > 0 ? ESP_USB_HOST_CCID_CARD_ISO_7816
                                       : ESP_USB_HOST_CCID_CARD_UNKNOWN;
  info.standardText = espUsbHostCcidCardStandardText(info.standard);

  // PC/SC synthetic ATR historical bytes:
  //   80 4F 0C A0 00 00 03 06 SS NN NN 00 00 00 00
  //   ^  ^  ^  ^--------------^  ^  ^--^
  //   |  |  |  RID              SS  card name
  //   |  |  length of the 0x4f TLV
  //   |  application identifier (TLV tag)
  //   category indicator "status information"
  static const uint8_t PCSC_RID[5] = {0xa0, 0x00, 0x00, 0x03, 0x06};
  // The TLV must be long enough for RID (5) + SS (1) + card name (2) and must
  // fit in the historical bytes.
  if (historicalLength < 3 || historical[0] != 0x80 || historical[1] != 0x4f || historical[2] < 8 ||
      historicalLength < static_cast<size_t>(3) + historical[2])
  {
    return true;
  }
  for (size_t i = 0; i < sizeof(PCSC_RID); i++)
  {
    if (historical[3 + i] != PCSC_RID[i])
    {
      return true;
    }
  }

  info.pcscStorageAtr = true;
  info.standardCode = historical[8];
  info.cardName = static_cast<uint16_t>((static_cast<uint16_t>(historical[9]) << 8) | historical[10]);
  info.cardNameText = espUsbHostCcidCardNameText(info.cardName);

  // PC/SC PIX.SS. The ISO codes are grouped per standard, one per level.
  switch (info.standardCode)
  {
  case 0x01:
  case 0x02:
  case 0x03:
    info.standard = ESP_USB_HOST_CCID_CARD_ISO_14443_A;
    info.level = static_cast<uint8_t>(info.standardCode);
    break;
  case 0x05:
  case 0x06:
  case 0x07:
    info.standard = ESP_USB_HOST_CCID_CARD_ISO_14443_B;
    info.level = static_cast<uint8_t>(info.standardCode - 0x04);
    break;
  case 0x09:
  case 0x0a:
  case 0x0b:
  case 0x0c:
    info.standard = ESP_USB_HOST_CCID_CARD_ISO_15693;
    info.level = static_cast<uint8_t>(info.standardCode - 0x08);
    break;
  case 0x0d:
  case 0x0e:
  case 0x0f:
  case 0x10:
    info.standard = ESP_USB_HOST_CCID_CARD_CONTACT_MEMORY;
    break;
  case 0x11:
    info.standard = ESP_USB_HOST_CCID_CARD_FELICA;
    break;
  case 0x40:
    info.standard = ESP_USB_HOST_CCID_CARD_LOW_FREQUENCY;
    break;
  default:
    // A PC/SC ATR that names a standard this table does not know. The raw code
    // is in standardCode; do not claim it is an ISO 7816 card.
    info.standard = ESP_USB_HOST_CCID_CARD_UNKNOWN;
    break;
  }
  info.standardText = espUsbHostCcidCardStandardText(info.standard);
  return true;
}

#endif
