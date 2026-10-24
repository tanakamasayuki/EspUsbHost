// en: Read a FeliCa IDm using a selected System Code.
// ja: 選んだSystem CodeでFeliCaのIDmを読む。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"
#include "Rcs300Device.hpp"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;
rcs300::Rcs300Reader reader(usb);

static constexpr uint32_t POLL_INTERVAL_MS = 1000;

static void printTarget(const char *label, uint16_t systemCode, const felica::Target &target)
{
  Serial.printf("%s SC=%04x IDm=", label, systemCode);
  rcs300::Rcs300Reader::printHex(target.idm, sizeof(target.idm));
  Serial.print("  PMm=");
  rcs300::Rcs300Reader::printHex(target.pmm, sizeof(target.pmm));
  if (target.hasRequestData)
  {
    // en: With Request Code 0x01 the target reports the system it answered from. After a wildcard poll this is the interesting part: it names what was reached.
    // ja: 要求で返されたSystem Codeを確認し、wildcardでどのsystemへ応答したかを区別する。
    Serial.printf("  answering system code=%04x\n", target.requestData);
  }
}

static void pollFor(const char *label, uint16_t systemCode)
{
  felica::Target target;
  if (reader.readTarget(systemCode, target))
  {
    printTarget(label, systemCode, target);
    return;
  }
  if (reader.noAnswer())
  {
    Serial.printf("%s SC=%04x no target answered\n", label, systemCode);
    return;
  }
  Serial.printf("%s SC=%04x failed result=0x%02x sw=%04x\n",
                label,
                systemCode,
                reader.lastResult(),
                reader.lastStatusWord());
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1000);

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected: address=%u vid=%04x pid=%04x product=\"%s\"\n",
                                        device.address, device.vid, device.pid, device.product); });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  usb.begin();
  Serial.println("EspUsbHost FeliCa IDm example start");
}

void loop()
{
  if (!reader.isOpen())
  {
    if (reader.open())
    {
      Serial.printf("RC-S300 ready: address=%u\n", reader.address());
    }
    else
    {
      delay(500);
      return;
    }
  }

  // en: Wildcard first, then the transit system. On a plain Suica both answer with the same IDm; on a phone they are what differ.
  // ja: wildcardと指定したsystemを順にpollingし、同じカードでも応答対象の違いを調べる。
  pollFor("wildcard", felica::SYSTEM_CODE_ANY);
  pollFor("transit ", felica::SYSTEM_CODE_TRANSIT);

  delay(POLL_INTERVAL_MS);
}
