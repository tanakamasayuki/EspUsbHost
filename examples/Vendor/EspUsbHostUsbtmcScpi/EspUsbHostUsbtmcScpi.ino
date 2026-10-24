// en: Exchange SCPI commands with a compatible USBTMC instrument.
// ja: 対応USBTMC計測器とSCPI命令をやり取りする。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。


#include "EspUsbHost.h"
#include "ScpiPmx.hpp"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;
pmx::ScpiPmx supply(usb);

// Conservative settings, well inside the PMX18-5A's 18V / 5A.
static constexpr float SET_VOLTAGE = 5.0f;
static constexpr float SET_CURRENT = 0.5f;

// en: This flag controls the request to enable output, not whether earlier voltage/current settings are written.
// ja: このflagは出力をonにする要求を制御する。先行する電圧・電流の書き込みを止めるものではない。
static constexpr bool TURN_OUTPUT_ON = false;

static constexpr uint32_t MEASURE_INTERVAL_MS = 1000;

static bool connected = false;

static void printCapabilities(const usbtmc::Capabilities &capabilities)
{
  Serial.printf("USBTMC %x.%02x listen_only=%u talk_only=%u indicator=%u termchar=%u\n",
                capabilities.bcdUsbtmc >> 8,
                capabilities.bcdUsbtmc & 0xff,
                capabilities.listenOnly ? 1 : 0,
                capabilities.talkOnly ? 1 : 0,
                capabilities.indicatorPulse ? 1 : 0,
                capabilities.termCharSupported ? 1 : 0);
  Serial.printf("USB488 usb488.2=%u remote_local=%u trigger=%u scpi=%u sr1=%u rl1=%u dt1=%u\n",
                capabilities.usb488_2 ? 1 : 0,
                capabilities.remoteLocalControl ? 1 : 0,
                capabilities.trigger ? 1 : 0,
                capabilities.scpi ? 1 : 0,
                capabilities.sr1 ? 1 : 0,
                capabilities.rl1 ? 1 : 0,
                capabilities.dt1 ? 1 : 0);
}

// en: Inspect the instrument error queue separately from USB transport errors; a completed transfer can still carry a rejected command.
// ja: 計測器のerror queueとUSB転送異常を別に調べる。転送が完了しても命令が拒否される場合がある。
static void printErrors()
{
  for (int i = 0; i < 8; i++)
  {
    int code = 0;
    char text[96] = {};
    if (!supply.error(code, text, sizeof(text)))
    {
      Serial.println("SYST:ERR? failed");
      return;
    }
    if (code == 0)
    {
      return;
    }
    Serial.printf("instrument error %d %s\n", code, text);
  }
}

static bool connect()
{
  if (!supply.begin())
  {
    return false;
  }

  usbtmc::UsbtmcDevice &tmc = supply.usbtmc();
  Serial.printf("USBTMC ready address=%u interface=%u usb488=%u bulk_out=0x%02x bulk_in=0x%02x mps=%u\n",
                tmc.address(),
                tmc.interfaceNumber(),
                tmc.isUsb488() ? 1 : 0,
                usb.vendorOutEndpoint(tmc.address()),
                usb.vendorInEndpoint(tmc.address()),
                usb.vendorOutPacketSize(tmc.address()));
  printCapabilities(tmc.capabilities());

  char idn[128] = {};
  if (!supply.identify(idn, sizeof(idn)))
  {
    Serial.println("*IDN? failed");
    return false;
  }
  Serial.printf("*IDN? %s\n", idn);

  if (!supply.setVoltage(SET_VOLTAGE) || !supply.setCurrent(SET_CURRENT))
  {
    Serial.println("setting voltage/current failed");
    return false;
  }

  float volts = 0.0f;
  float amps = 0.0f;
  if (supply.voltageSetting(volts) && supply.currentSetting(amps))
  {
    Serial.printf("setting readback %.3fV %.3fA\n", volts, amps);
  }

  if (TURN_OUTPUT_ON && !supply.setOutput(true))
  {
    Serial.println("enabling output failed");
    return false;
  }

  bool on = false;
  if (supply.output(on))
  {
    Serial.printf("output %s\n", on ? "ON" : "OFF");
  }

  printErrors();
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
        if (connected && device.address == supply.usbtmc().address())
        {
          supply.end();
          connected = false;
        } });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  usb.begin();
  Serial.println("EspUsbHostUsbtmcScpi start");
  Serial.println("Connect a KIKUSUI PMX series power supply with nothing on its output terminals.");
}

void loop()
{
  static uint32_t lastAttemptMs = 0;
  static uint32_t lastMeasureMs = 0;

  if (!connected)
  {
    if (millis() - lastAttemptMs >= 1000)
    {
      lastAttemptMs = millis();
      connected = connect();
    }
    return;
  }

  if (millis() - lastMeasureMs >= MEASURE_INTERVAL_MS)
  {
    lastMeasureMs = millis();
    float volts = 0.0f;
    float amps = 0.0f;
    if (supply.measureVoltage(volts) && supply.measureCurrent(amps))
    {
      Serial.printf("measured %.3fV %.3fA\n", volts, amps);
    }
    else
    {
      Serial.println("measurement failed");
      printErrors();
    }
  }
}
