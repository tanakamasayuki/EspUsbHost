// en: Issue explicit control and bulk requests from a diagnostic console.
// ja: 診断consoleからcontrol・bulk要求を明示的に送る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。


#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

// en: setup(8) + data must stay within the stack's 256-byte control transfer.
// ja: setup(8) + データがスタックの256バイト制限に収まる必要があります。
static constexpr size_t MAX_CONTROL_DATA = 248;
static constexpr size_t MAX_BULK_DATA = 512;
static constexpr size_t MAX_LINE = 160;

static uint8_t targetAddress = ESP_USB_HOST_ANY_ADDRESS;
static bool monitor = true;
static char line[MAX_LINE];
static size_t lineLength = 0;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i += 16)
  {
    Serial.printf("  %04x  ", (unsigned)i);
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

// en: Splits the line into whitespace-separated tokens, in place.
// ja: 行を空白区切りのトークンへその場で分割します。
static size_t tokenize(char *text, char **tokens, size_t maxTokens)
{
  size_t count = 0;
  char *cursor = text;
  while (*cursor && count < maxTokens)
  {
    while (*cursor == ' ' || *cursor == '\t')
    {
      cursor++;
    }
    if (!*cursor)
    {
      break;
    }
    tokens[count++] = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t')
    {
      cursor++;
    }
    if (*cursor)
    {
      *cursor++ = '\0';
    }
  }
  return count;
}

static uint32_t parseHex(const char *text)
{
  return (uint32_t)strtoul(text, nullptr, 16);
}

static uint8_t resolveAddress()
{
  if (targetAddress != ESP_USB_HOST_ANY_ADDRESS)
  {
    return targetAddress;
  }
  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  return count > 0 ? devices[0].address : ESP_USB_HOST_ANY_ADDRESS;
}

static void commandHelp()
{
  Serial.println("commands (numbers are hex):");
  Serial.println("  list                                     devices / interfaces / endpoints");
  Serial.println("  addr <a>                                 select the target device address");
  Serial.println("  open <iface> [ondemand]                  claim an interface for bulk transfers");
  Serial.println("  ctl <type> <req> <value> <index> <len|bytes...>   one EP0 control transfer");
  Serial.println("  out <bytes...>                           bulk OUT");
  Serial.println("  in [len] [timeout_ms]                    one bulk IN, waited for");
  Serial.println("  zlp                                      zero-length bulk OUT packet");
  Serial.println("  mon on|off                               unsolicited bulk IN printing");
  Serial.println("  desc                                     raw configuration descriptor");
  Serial.println("example: ctl 80 06 0100 0000 12            GET_DESCRIPTOR(DEVICE)");
}

static void commandList()
{
  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  if (count == 0)
  {
    Serial.println("no device connected");
    return;
  }
  for (size_t i = 0; i < count; i++)
  {
    const EspUsbHostDeviceInfo &device = devices[i];
    Serial.printf("device address=%u %04x:%04x \"%s\" \"%s\"%s\n",
                  device.address, device.vid, device.pid,
                  device.manufacturer, device.product,
                  device.address == resolveAddress() ? "  <= target" : "");

    EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
    const size_t interfaceCount = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
    EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
    const size_t endpointCount = usb.getEndpoints(device.address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);
    for (size_t n = 0; n < interfaceCount; n++)
    {
      Serial.printf("  interface %u class=0x%02x/0x%02x/0x%02x claimed=%s\n",
                    interfaces[n].number,
                    interfaces[n].interfaceClass,
                    interfaces[n].interfaceSubClass,
                    interfaces[n].interfaceProtocol,
                    interfaces[n].claimed ? "yes" : "no");
      for (size_t e = 0; e < endpointCount; e++)
      {
        if (endpoints[e].interfaceNumber != interfaces[n].number)
        {
          continue;
        }
        Serial.printf("    ep 0x%02x %-3s attrs=0x%02x max_packet=%u interval=%u\n",
                      endpoints[e].address,
                      (endpoints[e].address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) ? "IN" : "OUT",
                      endpoints[e].attributes,
                      endpoints[e].maxPacketSize,
                      endpoints[e].interval);
      }
    }
  }
}

static void commandOpen(char **tokens, size_t count)
{
  if (count < 2)
  {
    Serial.println("usage: open <iface> [ondemand]");
    return;
  }
  const uint8_t iface = (uint8_t)parseHex(tokens[1]);
  const bool onDemand = count >= 3 && strcmp(tokens[2], "ondemand") == 0;
  const EspUsbHostVendorReadMode mode = onDemand ? ESP_USB_HOST_VENDOR_READ_ON_DEMAND
                                                 : ESP_USB_HOST_VENDOR_READ_CONTINUOUS;
  const uint8_t address = resolveAddress();
  const bool ok = usb.vendorOpen(address, iface, mode);
  Serial.printf("open iface=%u mode=%s: %s\n", iface, onDemand ? "on-demand" : "continuous",
                ok ? "ok" : "failed");
  if (ok)
  {
    Serial.printf("  bulk out ep=0x%02x mps=%u / bulk in ep=0x%02x mps=%u\n",
                  usb.vendorOutEndpoint(address), usb.vendorOutPacketSize(address),
                  usb.vendorInEndpoint(address), usb.vendorInPacketSize(address));
  }
  else
  {
    // en: A refused claim is usually the library already owning that interface.
    // ja: claim拒否は、たいていライブラリ側が既にそのインターフェースを持っている場合です。
    Serial.printf("  %s (an interface already claimed by the library cannot be reopened)\n",
                  usb.lastErrorName());
  }
}

static void commandControl(char **tokens, size_t count)
{
  if (count < 5)
  {
    Serial.println("usage: ctl <bmRequestType> <bRequest> <wValue> <wIndex> <len|bytes...>");
    return;
  }
  const uint8_t requestType = (uint8_t)parseHex(tokens[1]);
  const uint8_t request = (uint8_t)parseHex(tokens[2]);
  const uint16_t value = (uint16_t)parseHex(tokens[3]);
  const uint16_t index = (uint16_t)parseHex(tokens[4]);
  const bool dataIn = (requestType & 0x80) != 0;

  static uint8_t buffer[MAX_CONTROL_DATA];
  size_t length = 0;
  if (dataIn)
  {
    length = count >= 6 ? (size_t)parseHex(tokens[5]) : 0;
    if (length > MAX_CONTROL_DATA)
    {
      Serial.printf("length capped at %u (control transfer limit)\n", (unsigned)MAX_CONTROL_DATA);
      length = MAX_CONTROL_DATA;
    }
  }
  else
  {
    for (size_t i = 5; i < count && length < MAX_CONTROL_DATA; i++)
    {
      buffer[length++] = (uint8_t)parseHex(tokens[i]);
    }
  }

  size_t actual = 0;
  const uint32_t startedAt = millis();
  const bool ok = usb.vendorControlTransfer(requestType, request, value, index,
                                            length > 0 ? buffer : nullptr, length,
                                            &actual, resolveAddress());
  Serial.printf("ctl type=0x%02x req=0x%02x value=0x%04x index=0x%04x len=%u: %s (%lums)\n",
                requestType, request, value, index, (unsigned)length,
                ok ? "ok" : "failed", (unsigned long)(millis() - startedAt));
  if (!ok)
  {
    // en: A stall here is the device's answer too: the request is unsupported.
    // ja: ここでのSTALLもデバイスの回答です。その要求は未サポートという意味になります。
    Serial.printf("  %s (a stalled request means the device rejected it)\n", usb.lastErrorName());
    return;
  }
  if (dataIn && actual > 0)
  {
    printHex(buffer, actual);
  }
}

static void commandOut(char **tokens, size_t count)
{
  static uint8_t buffer[MAX_BULK_DATA];
  size_t length = 0;
  for (size_t i = 1; i < count && length < MAX_BULK_DATA; i++)
  {
    buffer[length++] = (uint8_t)parseHex(tokens[i]);
  }
  if (length == 0)
  {
    Serial.println("usage: out <bytes...>");
    return;
  }
  const bool ok = usb.vendorWrite(buffer, length, resolveAddress());
  Serial.printf("out len=%u: %s\n", (unsigned)length, ok ? "ok" : "failed");
  if (!ok)
  {
    Serial.printf("  %s (is the interface open? see 'open')\n", usb.lastErrorName());
  }
}

static void commandIn(char **tokens, size_t count)
{
  static uint8_t buffer[MAX_BULK_DATA];
  size_t length = count >= 2 ? (size_t)parseHex(tokens[1]) : 64;
  if (length == 0 || length > MAX_BULK_DATA)
  {
    length = MAX_BULK_DATA;
  }
  const uint32_t timeoutMs = count >= 3 ? (uint32_t)parseHex(tokens[2]) : 1000;
  size_t actual = 0;
  const bool ok = usb.vendorReadSync(buffer, length, &actual, timeoutMs, resolveAddress());
  Serial.printf("in requested=%u received=%u: %s\n",
                (unsigned)length, (unsigned)actual, ok ? "ok" : "failed");
  if (ok && actual > 0)
  {
    printHex(buffer, actual);
  }
  else if (!ok)
  {
    Serial.printf("  %s (a timeout usually means the device answers only inside a transaction)\n",
                  usb.lastErrorName());
  }
}

static void handleLine(char *text)
{
  char *tokens[24];
  const size_t count = tokenize(text, tokens, 24);
  if (count == 0)
  {
    return;
  }

  if (strcmp(tokens[0], "help") == 0)
  {
    commandHelp();
  }
  else if (strcmp(tokens[0], "list") == 0)
  {
    commandList();
  }
  else if (strcmp(tokens[0], "addr") == 0)
  {
    targetAddress = count >= 2 ? (uint8_t)parseHex(tokens[1]) : ESP_USB_HOST_ANY_ADDRESS;
    Serial.printf("target address=%u\n", resolveAddress());
  }
  else if (strcmp(tokens[0], "open") == 0)
  {
    commandOpen(tokens, count);
  }
  else if (strcmp(tokens[0], "ctl") == 0)
  {
    commandControl(tokens, count);
  }
  else if (strcmp(tokens[0], "out") == 0)
  {
    commandOut(tokens, count);
  }
  else if (strcmp(tokens[0], "in") == 0)
  {
    commandIn(tokens, count);
  }
  else if (strcmp(tokens[0], "zlp") == 0)
  {
    Serial.printf("zlp: %s\n", usb.vendorWriteZlp(resolveAddress()) ? "ok" : "failed");
  }
  else if (strcmp(tokens[0], "mon") == 0)
  {
    monitor = !(count >= 2 && strcmp(tokens[1], "off") == 0);
    Serial.printf("monitor=%s\n", monitor ? "on" : "off");
  }
  else if (strcmp(tokens[0], "desc") == 0)
  {
    static uint8_t buffer[MAX_CONTROL_DATA];
    size_t actual = 0;
    const uint8_t address = resolveAddress();
    if (usb.vendorControlTransfer(0x80, 0x06, 0x0200, 0, buffer, 9, &actual, address) && actual >= 9)
    {
      size_t total = (size_t)buffer[2] | ((size_t)buffer[3] << 8);
      if (total > MAX_CONTROL_DATA)
      {
        Serial.printf("wTotalLength=%u; only the first %u bytes are readable here\n",
                      (unsigned)total, (unsigned)MAX_CONTROL_DATA);
        total = MAX_CONTROL_DATA;
      }
      if (usb.vendorControlTransfer(0x80, 0x06, 0x0200, 0, buffer, total, &actual, address))
      {
        printHex(buffer, actual);
      }
    }
    else
    {
      Serial.printf("desc failed: %s\n", usb.lastErrorName());
    }
  }
  else
  {
    Serial.printf("unknown command \"%s\" -- type help\n", tokens[0]);
  }
}

void setup()
{
  Serial.setTxBufferSize(4096);
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(3000);
  Serial.println("EspUsbHost protocol console start");
  commandHelp();

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected address=%u %04x:%04x \"%s\"\n",
                                        device.address, device.vid, device.pid, device.product); });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           { Serial.printf("disconnected address=%u\n", device.address); });

  // en: Bulk IN payloads that arrive without being asked for (continuous mode).
  // ja: 要求せずに届くバルクINのペイロード（continuousモード）。
  usb.onVendorData([](const EspUsbHostVendorData &data)
                   {
    if (!monitor)
    {
      return;
    }
    Serial.printf("in  address=%u iface=%u ep=0x%02x len=%u\n",
                  data.address, data.interfaceNumber, data.endpoint, (unsigned)data.length);
    printHex(data.data, data.length); });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  // en: Read only bytes already buffered; application message boundaries must be tracked separately.
  // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
  while (Serial.available() > 0)
  {
    const char c = (char)Serial.read();
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      line[lineLength] = '\0';
      handleLine(line);
      lineLength = 0;
      continue;
    }
    if (lineLength + 1 < MAX_LINE)
    {
      line[lineLength++] = c;
    }
  }
  delay(1);
}
