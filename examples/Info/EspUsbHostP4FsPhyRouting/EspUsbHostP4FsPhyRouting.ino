// en: Route the P4 full-speed PHY before starting USB Host.
// ja: P4のFullSpeed PHY経路を選んでからHostを開始する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <Arduino.h>
#include <EspUsbHost.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "EspUsbHostP4FsPhyRouting requires ESP32-P4"
#endif

#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(3000);

  Serial.println("Routing USB OTG FS to GPIO24/GPIO25 in 3 seconds.");
  Serial.println("USB Serial/JTAG on GPIO24/GPIO25 will disconnect.");
  delay(3000);

  // Route USB OTG FS to FSLS PHY0: GPIO24=D-, GPIO25=D+.
  // USB Serial/JTAG moves to FSLS PHY1: GPIO26=D-, GPIO27=D+.
  // This is a runtime override and does not modify eFuse.
  // en: Changing the PHY route can disconnect Serial/JTAG; check the schematic and independent log path first.
  // ja: PHY経路変更でSerial/JTAGが切れる場合がある。先に回路図と独立したログ経路を確認する。
  usb_wrap_ll_phy_select(&USB_WRAP, 0);

  EspUsbHostConfig config;
  config.port = ESP_USB_HOST_PORT_FULL_SPEED;

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin(config))
  {
    // en: This is visible only when Serial is available through another connection, such as an external USB-to-UART bridge.
    // ja: ログ経路が別に残っている場合だけ読める表示。USB切替前に独立したUART等を用意する。
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(10);
}
