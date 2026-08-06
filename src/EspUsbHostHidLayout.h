#ifndef ESP_USB_HOST_HID_LAYOUT_H
#define ESP_USB_HOST_HID_LAYOUT_H

// HID report descriptor decoding for mouse input reports, kept free of Arduino
// and USB dependencies so it compiles on the host and is covered by
// tests/unit/mouse_layout.
//
// A HID device is in report protocol after enumeration (HID 1.11 section
// 7.2.6), so what arrives on the interrupt endpoint is whatever the report
// descriptor declares -- not the 4-byte boot mouse report. The two coincide for
// simple mice (8 buttons, 8-bit X/Y/wheel), which is why decoding every mouse
// as a boot mouse worked for so long, but a gaming mouse that declares 16
// buttons and 16-bit axes lays its report out completely differently and gets
// shredded by the boot layout: the buttons overflow into X, X into Y and Y into
// the wheel.
//
// So the field positions are learned from the descriptor instead. Only the
// first Generic Desktop / Mouse application collection is described here;
// everything else in the descriptor is walked only to keep the per-report bit
// cursor correct.

#include <stddef.h>
#include <stdint.h>

static constexpr uint16_t ESP_USB_HOST_HID_PAGE_GENERIC_DESKTOP = 0x01;
static constexpr uint16_t ESP_USB_HOST_HID_PAGE_BUTTON = 0x09;
static constexpr uint16_t ESP_USB_HOST_HID_PAGE_CONSUMER = 0x0c;
static constexpr uint16_t ESP_USB_HOST_HID_USAGE_MOUSE = 0x02;
static constexpr uint16_t ESP_USB_HOST_HID_USAGE_X = 0x30;
static constexpr uint16_t ESP_USB_HOST_HID_USAGE_Y = 0x31;
static constexpr uint16_t ESP_USB_HOST_HID_USAGE_WHEEL = 0x38;
static constexpr uint16_t ESP_USB_HOST_HID_USAGE_AC_PAN = 0x0238;

// Buttons beyond this are ignored: the mask they are reported in is 16-bit, and
// no mouse in reach declares more.
static constexpr uint8_t ESP_USB_HOST_MOUSE_MAX_BUTTONS = 16;

struct EspUsbHostMouseReportField
{
  bool present = false;
  uint16_t bitOffset = 0; // from the start of the report body (after any ID)
  uint8_t bitSize = 0;
  bool isSigned = true; // logicalMin < 0
};

struct EspUsbHostMouseReportLayout
{
  // False when the descriptor declares no usable mouse: X and Y are the
  // minimum, buttons and wheel are optional.
  bool valid = false;
  uint8_t reportId = 0; // 0 when the descriptor declares no report IDs
  uint16_t reportBits = 0;
  uint16_t buttonsBitOffset = 0;
  uint8_t buttonCount = 0;
  EspUsbHostMouseReportField x;
  EspUsbHostMouseReportField y;
  EspUsbHostMouseReportField wheel;
  EspUsbHostMouseReportField pan; // AC Pan, the horizontal wheel
};

struct EspUsbHostMouseReportValues
{
  int32_t x = 0;
  int32_t y = 0;
  int32_t wheel = 0;
  int32_t pan = 0;
  uint16_t buttons = 0; // bit 0 = button 1
};

inline uint32_t espUsbHostHidExtractBits(const uint8_t *data, size_t length, uint16_t bitOffset, uint8_t bitSize)
{
  if (!data || bitSize == 0 || bitSize > 32)
  {
    return 0;
  }

  uint32_t value = 0;
  for (uint8_t bit = 0; bit < bitSize; bit++)
  {
    const size_t sourceBit = static_cast<size_t>(bitOffset) + bit;
    const size_t byteIndex = sourceBit / 8;
    if (byteIndex >= length)
    {
      break;
    }
    if (data[byteIndex] & (1U << (sourceBit % 8)))
    {
      value |= 1UL << bit;
    }
  }
  return value;
}

inline int32_t espUsbHostHidSignExtend(uint32_t value, uint8_t bitSize)
{
  if (bitSize == 0 || bitSize >= 32)
  {
    return static_cast<int32_t>(value);
  }
  const uint32_t signBit = 1UL << (bitSize - 1);
  if ((value & signBit) == 0)
  {
    return static_cast<int32_t>(value);
  }
  const uint32_t extendMask = ~((1UL << bitSize) - 1);
  return static_cast<int32_t>(value | extendMask);
}

namespace espUsbHostHidLayoutDetail
{

inline int32_t itemSigned(const uint8_t *data, size_t size)
{
  if (size == 0)
  {
    return 0;
  }
  uint32_t value = 0;
  for (size_t i = 0; i < size; i++)
  {
    value |= static_cast<uint32_t>(data[i]) << (8 * i);
  }
  return espUsbHostHidSignExtend(value, static_cast<uint8_t>(size * 8));
}

inline uint32_t itemUnsigned(const uint8_t *data, size_t size)
{
  uint32_t value = 0;
  for (size_t i = 0; i < size; i++)
  {
    value |= static_cast<uint32_t>(data[i]) << (8 * i);
  }
  return value;
}

struct GlobalItems
{
  uint16_t usagePage = 0;
  int32_t logicalMin = 0;
  int32_t logicalMax = 0;
  uint8_t reportSize = 0;
  uint8_t reportCount = 0;
  uint8_t reportId = 0;
};

// A local usage item can carry its own usage page in its upper 16 bits when it
// is 4 bytes wide; a narrower one takes the page from the global state.
struct LocalUsage
{
  uint32_t value = 0;
  bool extended = false;
};

inline uint16_t usagePageOf(const LocalUsage &usage, const GlobalItems &global)
{
  return usage.extended ? static_cast<uint16_t>(usage.value >> 16) : global.usagePage;
}

inline uint16_t usageOf(const LocalUsage &usage)
{
  return static_cast<uint16_t>(usage.value & 0xffff);
}

} // namespace espUsbHostHidLayoutDetail

// Learn where a mouse report keeps its buttons and axes. Returns layout.valid.
inline bool espUsbHostParseMouseReportLayout(const uint8_t *descriptor,
                                             size_t length,
                                             EspUsbHostMouseReportLayout &layout)
{
  using namespace espUsbHostHidLayoutDetail;

  layout = EspUsbHostMouseReportLayout();
  if (!descriptor || length == 0)
  {
    return false;
  }

  GlobalItems global;
  GlobalItems globalStack[8];
  size_t globalStackDepth = 0;

  LocalUsage usages[32];
  size_t usageCount = 0;
  LocalUsage usageMinimum;
  LocalUsage usageMaximum;
  bool hasUsageRange = false;

  // Input bit cursor per report ID. Report IDs are one byte, and output and
  // feature reports have their own bit space, so only Input items advance it.
  uint16_t bitOffsets[256] = {};

  int collectionDepth = 0;
  int mouseCollectionDepth = -1; // depth of the mouse application collection
  bool mouseSeen = false;        // stop after the first one has been described
  bool reportIdLocked = false;

  for (size_t i = 0; i < length;)
  {
    const uint8_t prefix = descriptor[i++];
    if (prefix == 0xfe) // long item: no defined tags, skip the payload
    {
      if (i + 1 >= length)
      {
        break;
      }
      const uint8_t itemLength = descriptor[i++];
      i++; // long item tag
      i += (i + itemLength <= length) ? itemLength : (length - i);
      continue;
    }

    const uint8_t sizeCode = prefix & 0x03;
    const size_t itemSize = sizeCode == 3 ? 4 : sizeCode;
    const uint8_t type = (prefix >> 2) & 0x03;
    const uint8_t tag = (prefix >> 4) & 0x0f;
    const size_t available = (i + itemSize <= length) ? itemSize : (length - i);
    const int32_t signedValue = itemSigned(&descriptor[i], available);
    const uint32_t unsignedValue = itemUnsigned(&descriptor[i], available);

    if (type == 1) // global
    {
      switch (tag)
      {
      case 0x00:
        global.usagePage = static_cast<uint16_t>(unsignedValue);
        break;
      case 0x01:
        global.logicalMin = signedValue;
        break;
      case 0x02:
        global.logicalMax = signedValue;
        break;
      case 0x07:
        global.reportSize = static_cast<uint8_t>(unsignedValue);
        break;
      case 0x08:
        global.reportId = static_cast<uint8_t>(unsignedValue);
        break;
      case 0x09:
        global.reportCount = static_cast<uint8_t>(unsignedValue);
        break;
      case 0x0a: // Push
        if (globalStackDepth < sizeof(globalStack) / sizeof(globalStack[0]))
        {
          globalStack[globalStackDepth++] = global;
        }
        break;
      case 0x0b: // Pop
        if (globalStackDepth > 0)
        {
          global = globalStack[--globalStackDepth];
        }
        break;
      default:
        break;
      }
    }
    else if (type == 2) // local
    {
      switch (tag)
      {
      case 0x00:
        if (usageCount < sizeof(usages) / sizeof(usages[0]))
        {
          usages[usageCount].value = unsignedValue;
          usages[usageCount].extended = available == 4;
          usageCount++;
        }
        break;
      case 0x01:
        usageMinimum.value = unsignedValue;
        usageMinimum.extended = available == 4;
        hasUsageRange = true;
        break;
      case 0x02:
        usageMaximum.value = unsignedValue;
        usageMaximum.extended = available == 4;
        hasUsageRange = true;
        break;
      default:
        break;
      }
    }
    else if (type == 0) // main
    {
      if (tag == 0x0a) // Collection
      {
        const uint8_t collectionType = static_cast<uint8_t>(unsignedValue & 0xff);
        if (!mouseSeen && mouseCollectionDepth < 0 && collectionType == 0x01 && usageCount > 0 &&
            usagePageOf(usages[0], global) == ESP_USB_HOST_HID_PAGE_GENERIC_DESKTOP &&
            usageOf(usages[0]) == ESP_USB_HOST_HID_USAGE_MOUSE)
        {
          mouseCollectionDepth = collectionDepth;
        }
        collectionDepth++;
      }
      else if (tag == 0x0c) // End Collection
      {
        if (collectionDepth > 0)
        {
          collectionDepth--;
        }
        if (mouseCollectionDepth >= 0 && collectionDepth <= mouseCollectionDepth)
        {
          mouseCollectionDepth = -1;
          mouseSeen = true;
        }
      }
      else if (tag == 0x08) // Input
      {
        const uint8_t flags = static_cast<uint8_t>(unsignedValue & 0xff);
        const bool constant = (flags & 0x01) != 0;
        const bool variable = (flags & 0x02) != 0;
        const uint8_t reportCount = global.reportCount == 0 ? 1 : global.reportCount;
        const uint8_t reportSize = global.reportSize;
        uint16_t &bitOffset = bitOffsets[global.reportId];
        const uint16_t fieldStartBit = bitOffset;

        const bool inMouse = mouseCollectionDepth >= 0;
        // Fields of a second report of the same mouse (a device that reports
        // buttons and axes separately is not described here) are skipped.
        const bool sameReport = !reportIdLocked || global.reportId == layout.reportId;

        if (inMouse && sameReport && !constant && variable && reportSize > 0)
        {
          // Buttons: a run of 1-bit fields on the Button page. Only the first
          // such run is taken; padding after it is constant and already
          // excluded.
          if (layout.buttonCount == 0 && reportSize == 1 &&
              ((hasUsageRange && usagePageOf(usageMinimum, global) == ESP_USB_HOST_HID_PAGE_BUTTON) ||
               (usageCount > 0 && usagePageOf(usages[0], global) == ESP_USB_HOST_HID_PAGE_BUTTON)))
          {
            layout.buttonsBitOffset = fieldStartBit;
            layout.buttonCount = reportCount > ESP_USB_HOST_MOUSE_MAX_BUTTONS
                                     ? ESP_USB_HOST_MOUSE_MAX_BUTTONS
                                     : reportCount;
            layout.reportId = global.reportId;
            reportIdLocked = true;
          }
          else
          {
            for (uint8_t field = 0; field < reportCount; field++)
            {
              LocalUsage usage;
              if (field < usageCount)
              {
                usage = usages[field];
              }
              else if (usageCount > 0 && !hasUsageRange)
              {
                // Fewer usages than fields: the HID spec repeats the last one.
                usage = usages[usageCount - 1];
              }
              else if (hasUsageRange)
              {
                usage = usageMinimum;
                usage.value += field;
                if (usage.value > usageMaximum.value)
                {
                  usage.value = usageMaximum.value;
                }
              }
              else
              {
                continue;
              }

              EspUsbHostMouseReportField *target = nullptr;
              const uint16_t page = usagePageOf(usage, global);
              const uint16_t usageId = usageOf(usage);
              if (page == ESP_USB_HOST_HID_PAGE_GENERIC_DESKTOP)
              {
                if (usageId == ESP_USB_HOST_HID_USAGE_X)
                {
                  target = &layout.x;
                }
                else if (usageId == ESP_USB_HOST_HID_USAGE_Y)
                {
                  target = &layout.y;
                }
                else if (usageId == ESP_USB_HOST_HID_USAGE_WHEEL)
                {
                  target = &layout.wheel;
                }
              }
              else if (page == ESP_USB_HOST_HID_PAGE_CONSUMER && usageId == ESP_USB_HOST_HID_USAGE_AC_PAN)
              {
                target = &layout.pan;
              }

              if (target && !target->present)
              {
                target->present = true;
                target->bitOffset = static_cast<uint16_t>(fieldStartBit + field * reportSize);
                target->bitSize = reportSize;
                target->isSigned = global.logicalMin < 0;
                layout.reportId = global.reportId;
                reportIdLocked = true;
              }
            }
          }
        }

        bitOffset = static_cast<uint16_t>(bitOffset + static_cast<uint16_t>(reportSize) * reportCount);
      }

      if (tag == 0x08 || tag == 0x09 || tag == 0x0b || tag == 0x0a || tag == 0x0c)
      {
        usageCount = 0;
        usageMinimum = LocalUsage();
        usageMaximum = LocalUsage();
        hasUsageRange = false;
      }
    }

    i += available;
    if (available < itemSize)
    {
      break; // truncated descriptor
    }
  }

  layout.reportBits = bitOffsets[layout.reportId];
  layout.valid = layout.x.present && layout.y.present;
  return layout.valid;
}

// Decode one input report with a learned layout. `report` is the transfer as it
// arrived, report ID prefix included.
inline bool espUsbHostDecodeMouseReport(const EspUsbHostMouseReportLayout &layout,
                                        const uint8_t *report,
                                        size_t length,
                                        EspUsbHostMouseReportValues &values)
{
  values = EspUsbHostMouseReportValues();
  if (!layout.valid || !report || length == 0)
  {
    return false;
  }

  const uint8_t *body = report;
  size_t bodyLength = length;
  if (layout.reportId != 0)
  {
    if (length < 2 || report[0] != layout.reportId)
    {
      return false;
    }
    body = report + 1;
    bodyLength = length - 1;
  }

  auto fits = [&](const EspUsbHostMouseReportField &field)
  {
    return field.present &&
           static_cast<size_t>(field.bitOffset) + field.bitSize <= bodyLength * 8;
  };
  auto read = [&](const EspUsbHostMouseReportField &field) -> int32_t
  {
    if (!fits(field))
    {
      return 0;
    }
    const uint32_t raw = espUsbHostHidExtractBits(body, bodyLength, field.bitOffset, field.bitSize);
    return field.isSigned ? espUsbHostHidSignExtend(raw, field.bitSize) : static_cast<int32_t>(raw);
  };

  // X and Y are what makes the report a mouse report; a transfer too short to
  // hold them is not one.
  if (!fits(layout.x) || !fits(layout.y))
  {
    return false;
  }

  values.x = read(layout.x);
  values.y = read(layout.y);
  values.wheel = read(layout.wheel);
  values.pan = read(layout.pan);

  for (uint8_t button = 0; button < layout.buttonCount; button++)
  {
    const size_t bit = static_cast<size_t>(layout.buttonsBitOffset) + button;
    if (bit >= bodyLength * 8)
    {
      break;
    }
    if ((body[bit / 8] >> (bit % 8)) & 0x01)
    {
      values.buttons |= static_cast<uint16_t>(1u << button);
    }
  }
  return true;
}

#endif // ESP_USB_HOST_HID_LAYOUT_H
