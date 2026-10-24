// en: Inspect a printer and optionally send an ESC/POS sample.
// ja: プリンタの状態を調べ、必要な場合だけESC/POSの試験印刷を行う。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。


#include "EspUsbHost.h"
#include "PrinterDevice.hpp"
#include "ReceiptJa.hpp"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;
printer::PrinterDevice slip(usb);

// en: Leave printing disabled while testing identification and status; enable it only for the intended physical print operation.
// ja: 識別と状態確認の間は印刷を無効にし、実際に印刷するときだけ有効にする。
static constexpr bool PRINT_SAMPLE = false;
// en: Cutting is an additional physical action after printing; confirm that the printer supports the selected cutter command.
// ja: cutは印刷後の追加動作。選択したcut命令にプリンタが対応するか確認する。
static constexpr bool CUT_PAPER = false;
// en: Japanese text needs a compatible printer font/encoding; transport success alone cannot prove the characters will render correctly.
// ja: 日本語には対応fontと文字コードが必要。転送成功だけで文字が正しく出るとは限らない。
static constexpr bool PRINT_JAPANESE = true;

static constexpr uint32_t STATUS_INTERVAL_MS = 5000;

static bool connected = false;
static bool printed = false;
static uint8_t printBuffer[printer::PRINT_BUFFER_SIZE];

static void printDeviceId()
{
  Serial.printf("device id \"%s\"\n", slip.deviceId());
  static const char *const keys[] = {"MFG", "MANUFACTURER", "MDL", "MODEL", "CMD", "COMMAND SET"};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
  {
    char value[64] = {};
    if (slip.deviceIdField(keys[i], value, sizeof(value)) > 0)
    {
      Serial.printf("  %s = %s\n", keys[i], value);
    }
  }
}

static void printStatus()
{
  printer::PortStatus port;
  if (slip.readPortStatus(port))
  {
    Serial.printf("port status 0x%02x paper_empty=%u selected=%u error=%u\n",
                  port.raw,
                  port.paperEmpty ? 1 : 0,
                  port.selected ? 1 : 0,
                  port.error ? 1 : 0);
  }
  else
  {
    Serial.println("GET_PORT_STATUS failed");
  }

  uint8_t value = 0;
  if (slip.realtimeStatus(escpos::STATUS_PRINTER, value))
  {
    Serial.printf("DLE EOT 1 printer 0x%02x offline=%u\n", value, (value & 0x08) ? 1 : 0);
  }
  if (slip.realtimeStatus(escpos::STATUS_PAPER_ROLL, value))
  {
    const escpos::PaperStatus paper = escpos::decodePaperStatus(value);
    Serial.printf("DLE EOT 4 paper 0x%02x near_end=%u out=%u\n",
                  paper.raw,
                  paper.nearEnd ? 1 : 0,
                  paper.out ? 1 : 0);
  }
}

// en: Check paper and error status before printing; status can change after the check, so still handle write failure.
// ja: 印刷前に用紙と異常を確認する。確認後も状態は変わるため、書き込み失敗を別に扱う。
static bool printSample()
{
  bool paperOut = false;
  bool nearEnd = false;
  bool error = false;
  if (!slip.checkPaper(paperOut, nearEnd, error))
  {
    Serial.println("cannot read the paper status; refusing to print");
    return false;
  }
  if (paperOut)
  {
    Serial.println("out of paper; refusing to print");
    return false;
  }
  if (error)
  {
    Serial.println("printer reports an error (cover open? cutter jam?); refusing to print");
    return false;
  }
  if (nearEnd)
  {
    Serial.println("paper near end");
  }

  escpos::Builder out(printBuffer, sizeof(printBuffer));
  if (PRINT_JAPANESE)
  {
    receipt::buildReceipt(out, CUT_PAPER, 12345678);
  }
  else
  {
    receipt::buildAsciiTest(out, CUT_PAPER);
  }
  if (!out.ok())
  {
    Serial.println("receipt does not fit in the print buffer");
    return false;
  }

  Serial.printf("printing %u bytes\n", static_cast<unsigned>(out.length()));
  if (!slip.write(out))
  {
    Serial.println("bulk write failed");
    return false;
  }
  return true;
}

static bool connect()
{
  if (!slip.begin())
  {
    return false;
  }

  Serial.printf("printer ready address=%u interface=%u protocol=0x%02x bidirectional=%u bulk_out=0x%02x bulk_in=0x%02x mps=%u\n",
                slip.address(),
                slip.interfaceNumber(),
                slip.protocol(),
                slip.isBidirectional() ? 1 : 0,
                usb.vendorOutEndpoint(slip.address()),
                usb.vendorInEndpoint(slip.address()),
                usb.vendorOutPacketSize(slip.address()));
  printDeviceId();
  printStatus();

  if (PRINT_SAMPLE && !printed)
  {
    printed = printSample();
  }
  else if (!PRINT_SAMPLE)
  {
    Serial.println("PRINT_SAMPLE is false; nothing was printed");
  }
  return true;
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(3000);

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                                        device.address,
                                        device.vid,
                                        device.pid,
                                        device.product); });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
        Serial.printf("disconnected address=%u\n", device.address);
        if (connected && device.address == slip.address())
        {
          slip.end();
          connected = false;
        } });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  usb.begin();
  Serial.println("EspUsbHostPrinterEscPos start");
  Serial.println("Connect an ESC/POS USB receipt printer with paper loaded.");
}

void loop()
{
  static uint32_t lastAttemptMs = 0;
  static uint32_t lastStatusMs = 0;

  if (!connected)
  {
    if (millis() - lastAttemptMs >= 1000)
    {
      lastAttemptMs = millis();
      connected = connect();
    }
    return;
  }

  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS)
  {
    lastStatusMs = millis();
    printStatus();
  }
}
