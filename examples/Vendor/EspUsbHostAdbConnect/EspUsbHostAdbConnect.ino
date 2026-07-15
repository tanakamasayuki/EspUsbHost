// EspUsbHostAdbConnect
//
// en: Skeleton that finds an Android ADB interface on a connected device,
//     claims it as a generic vendor-specific bulk interface, sends the ADB
//     A_CNXN handshake, and prints the device's first reply.
// ja: 接続されたデバイスからAndroid ADBインターフェースを探し、汎用の
//     vendor-specific bulkインターフェースとしてclaimし、ADBの A_CNXN
//     ハンドシェイクを送り、デバイスの最初の応答を表示する骨子です。
//
// en: This stops at the first reply. A real client must then handle A_AUTH
//     (RSA token signing / "always allow" dialog) before A_OPEN streams work.
// ja: これは最初の応答までです。実クライアントは A_OPEN ストリームの前に
//     A_AUTH（RSAトークン署名 / 「常に許可」ダイアログ）を処理する必要があります。

#include "EspUsbHost.h"

// en: ADB interface descriptor triple (see Android adb source, usb_descriptors).
// ja: ADBインターフェースの識別子(class/subclass/protocol)。
static const uint8_t ADB_CLASS = 0xff;
static const uint8_t ADB_SUBCLASS = 0x42;
static const uint8_t ADB_PROTOCOL = 0x01;

// en: ADB message commands (little-endian 4-byte tags).
// ja: ADBメッセージコマンド(リトルエンディアン4バイトタグ)。
static const uint32_t A_CNXN = 0x4e584e43; // 'CNXN'
static const uint32_t A_AUTH = 0x48545541; // 'AUTH'
static const uint32_t A_OKAY = 0x59414b4f; // 'OKAY'
static const uint32_t A_CLSE = 0x45534c43; // 'CLSE'
static const uint32_t A_WRTE = 0x45545257; // 'WRTE'
static const uint32_t A_OPEN = 0x4e45504f; // 'OPEN'

static const uint32_t A_VERSION = 0x01000000; // ADB protocol version
static const uint32_t A_MAXDATA = 4096;       // max payload we accept per message

EspUsbHost usb;

static uint8_t deviceAddress = 0;
static bool adbOpen = false;

// en: Linear reassembly buffer for incoming bulk IN bytes.
// ja: 受信bulk INバイトの再構成用リニアバッファ。
static uint8_t rxBuffer[512];
static size_t rxLength = 0;

static void putU32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t getU32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// en: ADB payload checksum is a plain byte sum, not a real CRC32.
// ja: ADBのペイロードチェックサムは本物のCRC32ではなく単純なバイト総和です。
static uint32_t adbChecksum(const uint8_t *data, size_t length)
{
  uint32_t sum = 0;
  for (size_t i = 0; i < length; i++)
  {
    sum += data[i];
  }
  return sum;
}

// en: Send one ADB message: 24-byte header followed by the optional payload.
// ja: ADBメッセージを1件送信：24バイトヘッダに続けて任意のペイロード。
static bool adbSend(uint32_t command, uint32_t arg0, uint32_t arg1, const uint8_t *data, uint32_t length)
{
  uint8_t header[24];
  putU32(header + 0, command);
  putU32(header + 4, arg0);
  putU32(header + 8, arg1);
  putU32(header + 12, length);
  putU32(header + 16, adbChecksum(data, length));
  putU32(header + 20, command ^ 0xffffffff); // magic
  if (!usb.vendorWrite(header, sizeof(header), deviceAddress))
  {
    return false;
  }
  if (length > 0)
  {
    return usb.vendorWrite(data, length, deviceAddress);
  }
  return true;
}

static void adbSendConnect()
{
  // en: Standard host banner. Some devices want a features list; "host::" is enough to trigger a reply.
  // ja: 標準のホストバナー。features一覧を要求する機種もあるが、応答を得るには "host::" で十分。
  static const char banner[] = "host::\0";
  Serial.printf("CNXN send: %s\n", adbSend(A_CNXN, A_VERSION, A_MAXDATA, (const uint8_t *)banner, sizeof(banner)) ? "ok" : "failed");
}

// en: Find the ADB interface number (class/subclass/protocol = ff/42/01) and claim it.
// ja: ADBインターフェース番号(class/subclass/protocol = ff/42/01)を探してclaimする。
static bool openAdbInterface()
{
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  const size_t count = usb.getInterfaces(deviceAddress, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  for (size_t i = 0; i < count; i++)
  {
    const EspUsbHostInterfaceInfo &itf = interfaces[i];
    if (itf.interfaceClass == ADB_CLASS && itf.interfaceSubClass == ADB_SUBCLASS && itf.interfaceProtocol == ADB_PROTOCOL)
    {
      Serial.printf("ADB interface found: number=%u\n", itf.number);
      if (usb.vendorOpen(deviceAddress, itf.number))
      {
        return true;
      }
      Serial.printf("vendorOpen(iface=%u) failed: %s\n", itf.number, usb.lastErrorName());
      return false;
    }
  }
  Serial.println("no ADB interface (class=0xff subclass=0x42 protocol=0x01) on this device");
  return false;
}

static const char *commandName(uint32_t command)
{
  switch (command)
  {
  case A_CNXN:
    return "CNXN";
  case A_AUTH:
    return "AUTH";
  case A_OKAY:
    return "OKAY";
  case A_CLSE:
    return "CLSE";
  case A_WRTE:
    return "WRTE";
  case A_OPEN:
    return "OPEN";
  default:
    return "????";
  }
}

// en: Parse complete ADB messages out of rxBuffer and report each one.
// ja: rxBufferから完結したADBメッセージを取り出して逐次報告する。
static void processReceived()
{
  while (rxLength >= 24)
  {
    const uint32_t command = getU32(rxBuffer + 0);
    const uint32_t arg0 = getU32(rxBuffer + 4);
    const uint32_t arg1 = getU32(rxBuffer + 8);
    const uint32_t dataLength = getU32(rxBuffer + 12);

    if (dataLength > sizeof(rxBuffer) - 24)
    {
      // en: Payload larger than our buffer; drop the header and resync.
      // ja: ペイロードがバッファより大きい。ヘッダを捨てて再同期する。
      Serial.printf("recv %s: data_length=%u exceeds buffer, dropping\n", commandName(command), (unsigned)dataLength);
      rxLength = 0;
      return;
    }
    if (rxLength < 24 + dataLength)
    {
      // en: Payload not fully arrived yet.
      // ja: ペイロードがまだ全部届いていない。
      return;
    }

    const uint8_t *payload = rxBuffer + 24;
    Serial.printf("recv %s arg0=0x%08x arg1=0x%08x len=%u", commandName(command), (unsigned)arg0, (unsigned)arg1, (unsigned)dataLength);
    if (command == A_CNXN)
    {
      Serial.print(" banner=");
      for (uint32_t i = 0; i < dataLength && payload[i] != 0; i++)
      {
        Serial.write(payload[i]);
      }
      Serial.println();
      Serial.println("-> device accepted connection (already authorized).");
    }
    else if (command == A_AUTH)
    {
      // en: arg0=1 (TOKEN): device sent a 20-byte challenge to be RSA-signed. Not implemented here.
      // ja: arg0=1(TOKEN)：デバイスがRSA署名すべき20バイトのチャレンジを送ってきた。ここでは未実装。
      Serial.println();
      Serial.println("-> device requests AUTH (RSA token). Signing is not implemented in this skeleton.");
      Serial.println("   Next step: sign the token with an RSA key and reply A_AUTH type=2, or send the public key (type=3).");
    }
    else
    {
      Serial.println();
    }

    // en: Shift out the consumed message.
    // ja: 消費したメッセージ分をシフトして詰める。
    const size_t consumed = 24 + dataLength;
    memmove(rxBuffer, rxBuffer + consumed, rxLength - consumed);
    rxLength -= consumed;
  }
}

void setup()
{
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost ADB connect skeleton start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          Serial.print("connected: ");
                          espUsbHostPrint(device);
                          rxLength = 0;
                          adbOpen = openAdbInterface();
                          if (adbOpen)
                          {
                            adbSendConnect();
                          } });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             adbOpen = false;
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (adbOpen)
  {
    // en: Drain bulk IN into the reassembly buffer, then parse whole messages.
    // ja: bulk INを再構成バッファへ吸い出し、完結したメッセージを解析する。
    if (rxLength < sizeof(rxBuffer))
    {
      const size_t got = usb.vendorRead(rxBuffer + rxLength, sizeof(rxBuffer) - rxLength, deviceAddress);
      if (got > 0)
      {
        rxLength += got;
        processReceived();
      }
    }

    // en: 'r' re-sends CNXN for manual retry.
    // ja: 'r' でCNXNを手動再送する。
    if (Serial.available() > 0 && Serial.read() == 'r')
    {
      adbSendConnect();
    }
  }
  delay(1);
}
