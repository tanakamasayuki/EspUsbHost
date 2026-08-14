// en: Identify an unknown USB device and decide how to drive it.
// ja: 未知のUSBデバイスを識別し、どう扱うかを決めるためのツールです。
//
// en: For every connected device it prints, in this order:
//       1. the parsed descriptor dump (printDeviceInfo)
//       2. one line per interface saying what that class is and which
//          EspUsbHost API / example covers it
//       3. the raw device and configuration descriptor bytes, plus a walk of
//          every descriptor block inside the configuration -- including the
//          class-specific ones (HID, CDC functional, CCID, UAC) that the
//          parsed dump does not show
//     Step 3 is what you compare against USBPcap/Wireshark or `lsusb -v`.
// ja: 接続された各デバイスについて、次の順に出力します:
//       1. 解析済みディスクリプタダンプ（printDeviceInfo）
//       2. インターフェースごとに、そのクラスの意味と対応するEspUsbHost API／
//          サンプルを1行で表示
//       3. デバイス／コンフィグレーションディスクリプタの生バイトと、
//          コンフィグレーション内の全ディスクリプタブロックの走査。解析済み
//          ダンプでは見えないクラス固有ディスクリプタ（HID/CDC機能/CCID/UAC）も含みます
//     手順3はUSBPcap/Wiresharkや`lsusb -v`と突き合わせるためのものです。
//
// en: Serial commands: 'd' dump everything again, 'r' raw descriptors only.
// ja: シリアルコマンド: 'd' 全体を再ダンプ、'r' 生ディスクリプタのみ。

#include "EspUsbHost.h"

EspUsbHost usb;

// en: ESP-IDF's precompiled host stack caps a control transfer at 256 bytes
//     including the 8-byte setup packet, so at most 248 descriptor bytes can be
//     read in one request. This is the same limit that stops USB cameras from
//     enumerating at all.
// ja: ESP-IDFのプリコンパイル済みホストスタックは、8バイトのsetupを含めて
//     コントロール転送を256バイトに制限しているため、1回で読めるディスクリプタは
//     最大248バイトです。USBカメラが列挙できないのと同じ制限です。
static constexpr size_t MAX_CONTROL_DATA = 248;

static constexpr uint32_t SETTLE_MS = 1500;
static uint32_t lastDeviceEventMs = 0;
static bool dumpPending = false;

struct ClassGuide
{
  const char *name;
  const char *how;
};

// en: What the interface class means, and how this library reaches it.
// ja: インターフェースクラスの意味と、このライブラリでの扱い方。
static ClassGuide guideFor(const EspUsbHostInterfaceInfo &intf)
{
  switch (intf.interfaceClass)
  {
  case 0x01:
    if (intf.interfaceSubClass == 0x03)
    {
      return {"Audio / MIDI Streaming", "library API: onMidiMessage() / sendMidi() -- examples/MIDI"};
    }
    if (intf.interfaceSubClass == 0x02)
    {
      return {"Audio Streaming (UAC)", "library API: onAudioData() / audioOutput* -- examples/Audio"};
    }
    return {"Audio Control (UAC)", "library API: the streaming interface carries the data -- examples/Audio"};
  case 0x02:
    if (intf.interfaceSubClass == 0x02)
    {
      return {"CDC Control (ACM)", "library API: EspUsbHostCdcSerial -- examples/Serial"};
    }
    if (intf.interfaceSubClass == 0x06)
    {
      return {"CDC Control (ECM Ethernet)", "library API: network -- examples/UsbNetwork"};
    }
    if (intf.interfaceSubClass == 0x0d)
    {
      return {"CDC Control (NCM Ethernet)", "library API: network -- examples/UsbNetwork"};
    }
    return {"CDC Control (other subclass)", "library API: try EspUsbHostCdcSerial, else vendorOpen()"};
  case 0x03:
    if (intf.interfaceSubClass == 0x01 && intf.interfaceProtocol == 0x01)
    {
      return {"HID boot keyboard", "library API: onKeyboard() -- examples/HID/EspUsbHostKeyboard"};
    }
    if (intf.interfaceSubClass == 0x01 && intf.interfaceProtocol == 0x02)
    {
      return {"HID boot mouse", "library API: onMouse() -- examples/HID/EspUsbHostMouse"};
    }
    return {"HID (report protocol)", "library API: onHIDInput()/onHIDVendorInput() -- examples/HID/EspUsbHostHIDRawDump"};
  case 0x05:
    return {"Physical (force feedback)", "no dedicated API; drive it as HID/vendor"};
  case 0x06:
    return {"Image (PTP/MTP camera)", "no dedicated API; bulk protocol via vendorOpen()"};
  case 0x07:
    return {"Printer", "example: vendorOpen() + ESC/POS -- examples/Vendor/EspUsbHostPrinterEscPos"};
  case 0x08:
    return {"Mass Storage (BOT/SCSI)", "library API: MSC block I/O and FatFs -- examples/Storage"};
  case 0x09:
    return {"Hub", "library API: topology and port power -- examples/Info/EspUsbHostHubPPPS"};
  case 0x0a:
    return {"CDC Data", "library API: paired with a CDC control interface -- examples/Serial"};
  case 0x0b:
    return {"Smart Card (CCID)", "library API: ccid* -- examples/Ccid"};
  case 0x0e:
    return {"Video (UVC camera)", "NOT SUPPORTED: the config descriptor exceeds the 256-byte control limit"};
  case 0x0f:
    return {"Personal Healthcare", "no dedicated API; usually a bulk/interrupt protocol via vendorOpen()"};
  case 0x11:
    return {"Billboard (USB-C alt mode)", "informational only; nothing to drive"};
  case 0xdc:
    return {"Diagnostic Device", "no dedicated API; vendorOpen()"};
  case 0xe0:
    return {"Wireless Controller (Bluetooth etc.)", "no dedicated API; vendorOpen()"};
  case 0xef:
    return {"Miscellaneous (IAD composite)", "look at the grouped interfaces below, not at this class"};
  case 0xfe:
    if (intf.interfaceSubClass == 0x01)
    {
      return {"Application Specific: DFU", "no dedicated API; EP0 class requests via vendorControlTransfer()"};
    }
    if (intf.interfaceSubClass == 0x03)
    {
      return {"Application Specific: USBTMC", "example: vendorOpen() + SCPI -- examples/Vendor/EspUsbHostUsbtmcScpi"};
    }
    return {"Application Specific", "no dedicated API; vendorOpen() / vendorControlTransfer()"};
  case 0xff:
    return {"Vendor-specific", "library API: vendorOpen() + bulk/EP0 -- examples/Vendor/EspUsbHostVendorBulk"};
  default:
    return {"unassigned/reserved class", "unknown; capture the traffic on a PC and drive it with vendorOpen()"};
  }
}

static const char *descriptorTypeName(uint8_t type)
{
  switch (type)
  {
  case 0x01:
    return "DEVICE";
  case 0x02:
    return "CONFIGURATION";
  case 0x03:
    return "STRING";
  case 0x04:
    return "INTERFACE";
  case 0x05:
    return "ENDPOINT";
  case 0x06:
    return "DEVICE_QUALIFIER";
  case 0x07:
    return "OTHER_SPEED_CONFIG";
  case 0x0b:
    return "INTERFACE_ASSOCIATION";
  case 0x21:
    return "HID / CDC-or-class-specific (0x21)";
  case 0x22:
    return "HID_REPORT";
  case 0x24:
    return "CS_INTERFACE (CDC/UAC/CCID functional)";
  case 0x25:
    return "CS_ENDPOINT";
  case 0x29:
    return "HUB";
  default:
    return "class/vendor specific";
  }
}

static void printHexBlock(const uint8_t *data, size_t length, const char *indent)
{
  for (size_t i = 0; i < length; i += 16)
  {
    Serial.printf("%s%04x  ", indent, (unsigned)i);
    for (size_t j = 0; j < 16; j++)
    {
      if (i + j < length)
      {
        Serial.printf("%02x ", data[i + j]);
      }
      else
      {
        Serial.print("   ");
      }
    }
    Serial.print(" |");
    for (size_t j = 0; j < 16 && i + j < length; j++)
    {
      const uint8_t c = data[i + j];
      Serial.write((c >= 0x20 && c < 0x7f) ? (char)c : '.');
    }
    Serial.println("|");
  }
}

// en: Walk the configuration descriptor block by block. Every descriptor starts
//     with bLength/bDescriptorType, which is all that is needed to step through
//     it -- including the class-specific blocks whose contents are not decoded.
// ja: コンフィグレーションディスクリプタをブロック単位で走査します。すべての
//     ディスクリプタはbLength/bDescriptorTypeで始まるので、内容を解釈できない
//     クラス固有ブロックも含め、これだけで辿れます。
static void walkConfiguration(const uint8_t *data, size_t length)
{
  size_t offset = 0;
  while (offset + 2 <= length)
  {
    const uint8_t blockLength = data[offset];
    const uint8_t type = data[offset + 1];
    if (blockLength < 2 || offset + blockLength > length)
    {
      Serial.printf("  offset=%3u truncated (bLength=%u, %u bytes left)\n",
                    (unsigned)offset, (unsigned)blockLength, (unsigned)(length - offset));
      return;
    }
    Serial.printf("  offset=%3u len=%2u type=0x%02x %-40s ",
                  (unsigned)offset, (unsigned)blockLength, type, descriptorTypeName(type));
    for (size_t i = 0; i < blockLength; i++)
    {
      Serial.printf("%02x", data[offset + i]);
      if (i + 1 < blockLength)
      {
        Serial.print(" ");
      }
    }
    Serial.println();
    offset += blockLength;
  }
}

static bool getDescriptor(uint8_t address, uint8_t type, uint8_t index, uint8_t *buffer, size_t length, size_t *actual)
{
  // en: Standard GET_DESCRIPTOR on EP0: bmRequestType=0x80, bRequest=0x06.
  // ja: EP0への標準GET_DESCRIPTOR: bmRequestType=0x80, bRequest=0x06。
  return usb.vendorControlTransfer(0x80, 0x06, (uint16_t)((type << 8) | index), 0, buffer, length, actual, address);
}

static void dumpRawDescriptors(uint8_t address)
{
  static uint8_t buffer[MAX_CONTROL_DATA];
  size_t actual = 0;

  Serial.println();
  Serial.printf("--- raw DEVICE descriptor (address=%u) ---\n", address);
  if (getDescriptor(address, 0x01, 0, buffer, 18, &actual) && actual > 0)
  {
    printHexBlock(buffer, actual, "  ");
  }
  else
  {
    Serial.printf("  GET_DESCRIPTOR(DEVICE) failed: %s\n", usb.lastErrorName());
  }

  Serial.printf("--- raw CONFIGURATION descriptor (address=%u) ---\n", address);
  if (!getDescriptor(address, 0x02, 0, buffer, 9, &actual) || actual < 9)
  {
    Serial.printf("  GET_DESCRIPTOR(CONFIGURATION) failed: %s\n", usb.lastErrorName());
    return;
  }
  const size_t totalLength = (size_t)buffer[2] | ((size_t)buffer[3] << 8);
  size_t request = totalLength;
  if (request > MAX_CONTROL_DATA)
  {
    request = MAX_CONTROL_DATA;
    Serial.printf("  wTotalLength=%u exceeds the %u-byte control transfer limit;"
                  " only the first %u bytes can be read here.\n",
                  (unsigned)totalLength, (unsigned)MAX_CONTROL_DATA, (unsigned)MAX_CONTROL_DATA);
    Serial.println("  A device whose configuration descriptor is longer than 256 bytes in total");
    Serial.println("  cannot enumerate on this stack at all (this is why UVC cameras do not work).");
  }
  if (!getDescriptor(address, 0x02, 0, buffer, request, &actual) || actual == 0)
  {
    Serial.printf("  GET_DESCRIPTOR(CONFIGURATION, %u) failed: %s\n", (unsigned)request, usb.lastErrorName());
    return;
  }
  Serial.printf("  wTotalLength=%u read=%u bytes\n", (unsigned)totalLength, (unsigned)actual);
  printHexBlock(buffer, actual, "  ");
  Serial.println("--- configuration descriptor blocks ---");
  walkConfiguration(buffer, actual);
}

static void explainInterfaces(const EspUsbHostDeviceInfo &device)
{
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  const size_t interfaceCount = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
  const size_t endpointCount = usb.getEndpoints(device.address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);

  Serial.println();
  Serial.printf("--- how to drive address=%u (%04x:%04x) ---\n", device.address, device.vid, device.pid);
  for (size_t i = 0; i < interfaceCount; i++)
  {
    const EspUsbHostInterfaceInfo &intf = interfaces[i];
    const ClassGuide guide = guideFor(intf);
    Serial.printf("interface %u alt=%u class=0x%02x/0x%02x/0x%02x claimed=%s\n",
                  intf.number, intf.alternate,
                  intf.interfaceClass, intf.interfaceSubClass, intf.interfaceProtocol,
                  intf.claimed ? "yes" : "no");
    Serial.printf("  what : %s\n", guide.name);
    Serial.printf("  how  : %s\n", guide.how);
    for (size_t e = 0; e < endpointCount; e++)
    {
      const EspUsbHostEndpointInfo &ep = endpoints[e];
      if (ep.interfaceNumber != intf.number)
      {
        continue;
      }
      const bool in = (ep.address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
      const uint8_t type = ep.attributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
      const char *typeName = "control";
      if (type == USB_BM_ATTRIBUTES_XFER_ISOC)
      {
        typeName = "isochronous";
      }
      else if (type == USB_BM_ATTRIBUTES_XFER_BULK)
      {
        typeName = "bulk";
      }
      else if (type == USB_BM_ATTRIBUTES_XFER_INT)
      {
        typeName = "interrupt";
      }
      Serial.printf("  ep   : 0x%02x %-3s %-11s max_packet=%u interval=%u\n",
                    ep.address, in ? "IN" : "OUT", typeName, ep.maxPacketSize, ep.interval);
    }
  }
  if (interfaceCount == 0)
  {
    Serial.println("no interfaces reported -- the device did not finish enumerating");
  }
}

static void dumpAll(bool includeParsed)
{
  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  if (count == 0)
  {
    Serial.println("no device connected");
    return;
  }
  if (includeParsed)
  {
    usb.printAllDeviceInfo();
  }
  for (size_t i = 0; i < count; i++)
  {
    if (includeParsed)
    {
      explainInterfaces(devices[i]);
    }
    dumpRawDescriptors(devices[i].address);
  }
  Serial.println();
  Serial.println("=== end of dump ('d' dump again, 'r' raw descriptors only) ===");
}

void setup()
{
  // en: The dump bursts faster than the default TX buffer drains.
  // ja: ダンプは既定のTXバッファ排出より速くバーストします。
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(3000);
  Serial.println("EspUsbHost device explorer start");
  Serial.println("Plug in the device to inspect. Commands: 'd' dump, 'r' raw descriptors.");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
    lastDeviceEventMs = millis();
    dumpPending = true;
    Serial.printf("\nconnected address=%u %04x:%04x \"%s\"\n",
                  device.address, device.vid, device.pid, device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
    lastDeviceEventMs = millis();
    Serial.printf("disconnected address=%u %04x:%04x\n", device.address, device.vid, device.pid); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
  lastDeviceEventMs = millis();
}

void loop()
{
  // en: Wait for enumeration to settle: a composite device raises several events.
  // ja: 列挙が落ち着くのを待ちます。コンポジットデバイスは複数のイベントを上げます。
  if (dumpPending && millis() - lastDeviceEventMs >= SETTLE_MS)
  {
    dumpPending = false;
    dumpAll(true);
  }

  while (Serial.available() > 0)
  {
    const char command = (char)Serial.read();
    if (command == 'd')
    {
      dumpAll(true);
    }
    else if (command == 'r')
    {
      dumpAll(false);
    }
  }
  delay(10);
}
